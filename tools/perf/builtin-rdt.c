#include "perf.h"
#include "builtin.h"
#include "debug.h"
#include "workload.h"
#include "env.h"
#include "header.h"
#include <linux/compiler.h>
#include <subcmd/parse-options.h>
#include <api/fs/fs.h>
#include <sys/types.h>
#include <sys/stat.h>

static const char *resctrlfs;

enum {
	RDT_RESOURCE_L3,
	RDT_RESOURCE_L3DATA,
	RDT_RESOURCE_L3CODE,
	RDT_RESOURCE_L2,
	RDT_NUM_RESOURCES,
};

struct rdt_resource {
	const char		*name;
	bool			 enabled;
	u64			 cbm_mask;
	u64			 min_cbm_bits;
	u64			 num_closids;
	struct {
		struct cpu_cache_level	 *cache;
		char			**name;
		int			  cnt;
		int			  width;
	} dom;
};

struct rdt_cbm {
	u64	id;
	u64	val;
};

struct rdt_schemata {
	int		 cnt;
	struct rdt_cbm	*cbm;
};

struct rdt_group {
	const char		*name;
	struct rdt_schemata	 schemata[RDT_NUM_RESOURCES];
	struct list_head	 list;
};

static struct rdt_resource resource[RDT_NUM_RESOURCES];
static LIST_HEAD(groups);
static int groups_cnt;
static int groups_width;

static char *cache_name(struct cpu_cache_level *c)
{
	char buf[1000];

	scnprintf(buf, 1000, "ID %d (L%d %s %s %s)",
		  c->id, c->level, c->type, c->size, c->map);
	return strdup(buf);
}

#define MAX_CACHES 2000

static int load_cache(int r, struct rdt_resource *res)
{
	static u32 level[RDT_NUM_RESOURCES] = { 3, 3, 3, 2 };
	struct cpu_cache_level cache[MAX_CACHES], *c;
	char *name[MAX_CACHES];
	u32 i, j, cnt = 0;
        int ret, width = 0;

        ret = perf_build_caches(cache, MAX_CACHES, &cnt);
	if (ret) {
		pr_err("failed: to get cache details\n");
		return -1;
	}

	for (i = 0, j = 0; i < cnt; i++) {
		c = &cache[i];

		if (c->level != level[r])
			continue;

		cache[j] = *c;
		name[j]  = cache_name(c);
		width    = max(width, (int) strlen(name[j]));
		j++;
	}

	res->dom.cache = memdup(cache, j * sizeof(*c));
	res->dom.name  = memdup(name,  j * sizeof(char *));
	res->dom.width = width;
	res->dom.cnt   = j;
	return res->dom.cache ? 0 : -ENOMEM;
}

static int load_resource(int i, struct rdt_resource *res)
{
	char info[PATH_MAX];
	char path[PATH_MAX];
	static const char *name[RDT_NUM_RESOURCES] = {
		"L3", "L3DATA", "L3CODE", "L2",
	};
	struct stat st;
	unsigned long long val;

	res->name = name[i];

	scnprintf(info, PATH_MAX, "%s/info/%s", resctrlfs, res->name);
	if (stat(info, &st))
		return 0;

	res->enabled = true;

	scnprintf(path, PATH_MAX, "%s/cbm_mask", info, res->name);
	if (filename__read_ull(path, &val)) {
		pr_err("failed: read cbm_mask for %s\n", res->name);
		return -1;
	}

	res->cbm_mask = val;

	scnprintf(path, PATH_MAX, "%s/min_cbm_bits", info , res->name);
	if (filename__read_ull(path, &val)) {
		pr_err("failed: read min_cbm_bits for %s\n", res->name);
		return -1;
	}

	res->min_cbm_bits = val;

	scnprintf(path, PATH_MAX, "%s/num_closids", info, res->name);
	if (filename__read_ull(path, &val)) {
		pr_err("failed: read num_closids for %s\n", res->name);
		return -1;
	}

	res->num_closids = val;

	return load_cache(i, res);
}


static int load_resources(struct rdt_resource res[])
{
	int i, ok = 0;

	for (i = 0; i < RDT_NUM_RESOURCES; i++) {
		if (load_resource(i, &res[i]))
			return -1;
		if (res[i].enabled)
			ok++;
	}

	return ok ? 0 : -1;
}

static struct rdt_group *rdt_group__alloc(const char *name)
{
	struct rdt_group *group = zalloc(sizeof(*group));

	if (!group)
		return NULL;

	INIT_LIST_HEAD(&group->list);
	group->name = strdup(name);
	return group;
}

static int get_resource(char *line)
{
	static const char *name[RDT_NUM_RESOURCES] = {
		"L3", "L3DATA", "L3CODE", "L2",
	};
	int i;

	for (i = 0; i < RDT_NUM_RESOURCES; i++) {
		if (!strncmp(name[i], line, strlen(name[i])))
			return i;
	}

	return -1;
}

static int add_ass(struct rdt_schemata *schemata, char *ass)
{
	char *cbms, *ids = ass;
	int id;
	u64 cbm;

	cbms = strchr(ass, '=');
	if (!cbms)
		return -EINVAL;

	*cbms++ = 0;

	cbm = strtoul(cbms, NULL, 16);
	id  = strtoul(ids, NULL, 10);

	schemata->cbm = realloc(schemata->cbm, (schemata->cnt + 1) * sizeof(struct rdt_cbm));
	if (schemata->cbm) {
		schemata->cbm[schemata->cnt].id  = id;
		schemata->cbm[schemata->cnt].val = cbm;
		schemata->cnt++;
	}

	return schemata->cbm ? 0 : -ENOMEM;
}

static int parse_schemata(char *line, struct rdt_schemata *schemata)
{
	char *buf, *tmp = NULL, *ass;

	buf = strchr(line, ':');
	if (!buf)
		return -EINVAL;

	buf++;

	ass = strtok_r(buf, ";", &tmp);
	while (ass) {
		add_ass(schemata, ass);
		ass = strtok_r(NULL, ";", &tmp);
	};

	return 0;
}

static int rdt_group__load(struct rdt_group *group, const char *path)
{
	char file[PATH_MAX];
	size_t size;
	char *buf, *tmp, *line;

	scnprintf(file, PATH_MAX, "%s/schemata", path);
	if (filename__read_str(file, &buf, &size)) {
		pr_err("failed: read schemata for %s\n", group->name);
		return -1;
	}

	line = strtok_r(buf, "\n", &tmp);
	while (line) {
		int r = get_resource(line);

		if (r >= 0)
			parse_schemata(line, &group->schemata[r]);

		line = strtok_r(NULL, "\n", &tmp);
	}

	free(buf);
	return 0;
}

static int add_group(const char *name, const char *path, struct list_head *head)
{
	struct rdt_group *group = rdt_group__alloc(name);

	if (!group)
		return -ENOMEM;

	if (rdt_group__load(group, path))
		return -1;

	list_add_tail(&group->list, head);
	return 0;
}

static int load_groups(struct list_head *head, int *width)
{
	struct dirent *entry;
	DIR *dir;
	int nr = 1;

	if (add_group("default", resctrlfs, head))
		return -1;

	*width = strlen("default");

	dir = opendir(resctrlfs);
	if (!dir)
		return -1;

	while ((entry = readdir(dir))) {
		char path[PATH_MAX];

		if (entry->d_type != DT_DIR)
			continue;

		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0 ||
		    strcmp(entry->d_name, "info") == 0)
			continue;

		scnprintf(path, PATH_MAX, "%s/%s", resctrlfs, entry->d_name);

		if (add_group(entry->d_name, path, head))
			return -1;
		nr++;
		*width = max(*width, (int) strlen(entry->d_name));
	}

	closedir(dir);
	return nr;
}

static int setup_resctrl(void)
{
	resctrlfs = resctrlfs__mount();
	if (!resctrlfs) {
		pr_err("failed: no resctrl fs mount found\n");
		return -1;
	}

	if (load_resources(resource)) {
		pr_err("failed: load rdt resources\n");
		return -1;
	}

	groups_cnt = load_groups(&groups, &groups_width);
	if (groups_cnt < 0) {
		pr_err("failed: load rdt groups\n");
		return -1;
	}

	return 0;
}

static int move_task(int pid, const char *group)
{
	char path[PATH_MAX], buf[20];
	struct stat st;
	FILE *file;
	int cnt;

	scnprintf(path, PATH_MAX, "%s/%s/tasks", resctrlfs, group);

	if (stat(path, &st)) {
		pr_err("failed: group not found\n");
		return -1;
	}

	file = fopen(path, "w");
	if (!file) {
		pr_err("failed: open file '%s'\n", path);
		return -1;
	}

	cnt = scnprintf(buf, 20, "%d\n", pid);
	if (cnt != (int) fwrite(buf, cnt, 1, file))
		pr_err("failed: to write to '%s'\n", path);

	fclose(file);
	return 0;
}

int cmd_rdt(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const char * const rdt_usage[] = {
		"perf rdt [<options>] <command>",
		"perf rdt [<options>] -- <command> [<options>]",
		NULL
	};
	static const char *group;
	const struct option rdt_options[] = {
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_STRING('g', "group", &group, "group",
		   "group to attach workload to"),
	OPT_END()
	};

	if (setup_resctrl())
		return -1;

	argc = parse_options(argc, argv, rdt_options, rdt_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc)
		usage_with_options(rdt_usage, rdt_options);

	if (group) {
		struct perf_workload workload;
		int status;

		if (perf_workload__prepare(&workload, argv, false, NULL))
			return -1;

		if (move_task(workload.pid, group))
			return -1;

		if (perf_workload__start(&workload))
			return -1;

		wait(&status);
	}

	return 0;
}
