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

static struct rdt_resource resource[RDT_NUM_RESOURCES];

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
