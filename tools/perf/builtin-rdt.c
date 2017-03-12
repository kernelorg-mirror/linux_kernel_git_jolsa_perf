#include "perf.h"
#include "builtin.h"
#include "debug.h"
#include "workload.h"
#include "env.h"
#include "header.h"
#include "ui/browser.h"
#include "ui/keysyms.h"
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

struct rdt_browser {
	struct ui_browser	 b;
	int			 cur;
	struct {
		u16	cnt;
		u16	vis;
		u16	sta;
		u16	cur;
	} col;
};

static struct rdt_resource resource[RDT_NUM_RESOURCES];
static LIST_HEAD(groups);
static int groups_cnt;
static int groups_width;

static char *cache_name(struct cpu_cache_level *c)
{
	char buf[1000];

	scnprintf(buf, 1000, "ID %d (%s)", c->id, c->map);
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
	res->dom.width = width + 1;
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

	(*width)++;

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

static void display_resource(struct rdt_resource *res)
{
	int i;

	fprintf(stdout, "  %s mask(0x%lx) min(%ld) ids(%ld)\n",
		res->name, res->cbm_mask, res->min_cbm_bits, res->num_closids);

	for (i = 0; i < res->dom.cnt; i++)
		fprintf(stdout, "    %s\n", res->dom.name[i]);
}

static void display_schemata(struct rdt_schemata *schemata)
{
	int i;

	for (i = 0; i < schemata->cnt; i++) {
		fprintf(stdout, "      ID %ld = 0x%lx\n",
				 schemata->cbm[i].id, schemata->cbm[i].val);
	}
}

static void display_group(struct rdt_group *group)
{
	struct rdt_resource *res;
	int i;

	fprintf(stdout, "  %s\n", group->name);

	for (i = 0; i < RDT_NUM_RESOURCES; i++) {
		res = &resource[i];

		if (res->enabled) {
			fprintf(stdout, "    %s\n", res->name);
			display_schemata(&group->schemata[i]);
		}
	}
}

static int do_list(void)
{
	struct rdt_resource *res;
	struct rdt_group *group;
	int i;

	fprintf(stdout, "Enabled resources:\n");

	for (i = 0; i < RDT_NUM_RESOURCES; i++) {
		res = &resource[i];

		if (res->enabled)
			display_resource(res);
	}

	fprintf(stdout, "Enabled groups:\n");

	list_for_each_entry(group, &groups, list) {
		display_group(group);
	}

	return 0;
}

static void rdt_browser__write(struct ui_browser *b, void *entry, int row)
{
	struct rdt_browser *browser = container_of(b, struct rdt_browser, b);
	struct rdt_group *group = list_entry(entry, struct rdt_group, list);
	struct rdt_resource *res;
	bool current_entry = ui_browser__is_current_entry(b, row);
	int color = current_entry ? HE_COLORSET_SELECTED : HE_COLORSET_NORMAL;
	int cnt, ret, i, width = b->width;
	char buf[100];

	ret = scnprintf(buf, width, "%-*s", groups_width, group->name);
	ui_browser__set_color(b, color);
	ui_browser__printf(b, "%s", buf);

	res = &resource[browser->cur];
	cnt = browser->col.sta + browser->col.vis;

	for (i = browser->col.sta; i < cnt; i++) {
		char val[100];
		int col = color;

		scnprintf(val, 100, "0x%lx", group->schemata[browser->cur].cbm[i].val);
		ret += scnprintf(buf, width - ret, "%-*s", res->dom.width, val);

		if (current_entry && browser->col.cur == i)
			col = HE_COLORSET_TOP;

		ui_browser__set_color(b, col);
		ui_browser__printf(b, "%s", buf);
	}

	width -= ret;
	ui_browser__write_nstring(b, "", width);
}

static void next_resource(struct rdt_browser *browser)
{
	int cur = browser->cur;

	do {
		cur = (cur + 1) % RDT_NUM_RESOURCES;
	} while (!resource[cur].enabled);

	browser->cur = cur;
}

static void rdt_browser__title(struct rdt_browser *browser,
			       char *title, int size)
{
	struct rdt_resource *res;

	res = &resource[browser->cur];
	scnprintf(title, size, "RDT resource: %s %s %s\n",
		  res->name, res->dom.cache[0].type, res->dom.cache[0].size);
}

static struct rdt_resource *current(struct rdt_browser *browser)
{
	return &resource[browser->cur];
}

static void rdt_browser__refresh_dimensions(struct ui_browser *b)
{
	struct rdt_browser *browser = container_of(b, struct rdt_browser, b);
	struct rdt_resource *res;

	ui_browser__refresh_dimensions(b);
	b->y++;

	res = current(browser);
	browser->col.cnt = res->dom.cnt;
	browser->col.vis = min(res->dom.cnt, (b->width - groups_width) / res->dom.width);
	browser->col.sta = 0;
	browser->col.cur = 0;
}

static void rdt_browser__horiz_scroll(struct rdt_browser *b, bool left)
{
	if (left) {
		if (b->col.cur > 0)
			b->col.cur--;
		if (b->col.cur < b->col.sta)
			b->col.sta = b->col.cur;
	} else {
		if ((b->col.cur + 1) < b->col.cnt)
			b->col.cur++;
		if (b->col.cur - b->col.sta >= b->col.vis)
			b->col.sta++;
	}
}

static int rdt_browser__run(struct rdt_browser *browser)
{
	char title[200];
	int key;

	rdt_browser__title(browser, title, sizeof(title));

	if (ui_browser__show(&browser->b, title,
			     "Press ESC to exit") < 0)
		return -1;

	while (1) {
		key = ui_browser__run(&browser->b, 0);

		switch (key) {
		default:
			break;
		case K_TAB:
			next_resource(browser);
			rdt_browser__title(browser, title, sizeof(title));
			rdt_browser__refresh_dimensions(&browser->b);
			ui_browser__show_title(&browser->b, title);
			break;
		case K_LEFT:
			rdt_browser__horiz_scroll(browser, true);
			break;
		case K_RIGHT:
			rdt_browser__horiz_scroll(browser, false);
			break;
		case K_ESC:
		case 'q':
		case CTRL('c'):
			goto out;
		}
	}
out:
	ui_browser__hide(&browser->b);
	return 0;
}

static int headers_scnprintf(struct rdt_browser *browser, char *buf, int size)
{
	struct rdt_resource *res;
	int cnt, i, ret = 0;

	res = &resource[browser->cur];

	ret = scnprintf(buf, size - ret, "%-*s", groups_width, "Group");
	cnt = browser->col.sta + browser->col.vis;

	for (i = browser->col.sta; i < cnt; i++) {
		ret += scnprintf(buf + ret, size - ret, "%-*s", res->dom.width, res->dom.name[i]);
	}

	return ret;
}

static int display_headers(struct rdt_browser *browser)
{
	int width = browser->b.width + 1;
	char *buf;

	buf = zalloc(width);
	if (!buf)
		return -ENOMEM;

	width -= headers_scnprintf(browser, buf, width);

	SLsmg_gotorc(1, 0);
	ui_browser__set_color(&browser->b, HE_COLORSET_ROOT);
	ui_browser__printf(&browser->b, "%s", buf);
	ui_browser__write_nstring(&browser->b, "", width);

	free(buf);
	return 0;
}

static unsigned int rdt_browser__refresh(struct ui_browser *b)
{
	struct rdt_browser *browser = container_of(b, struct rdt_browser, b);

	display_headers(browser);
	return ui_browser__list_head_refresh(b);
}

static int cmd_rdt_tui(void)
{
	struct rdt_browser browser = {
		.b      = {
			.seek			= ui_browser__list_head_seek,
			.refresh		= rdt_browser__refresh,
			.write			= rdt_browser__write,
			.refresh_dimensions	= rdt_browser__refresh_dimensions,
			.entries		= &groups,
			.nr_entries		= groups_cnt,
		},
	};

	use_browser = 1;
	setup_browser(true);
	return rdt_browser__run(&browser);
}

int cmd_rdt(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const char * const rdt_usage[] = {
		"perf rdt [<options>] <command>",
		"perf rdt [<options>] -- <command> [<options>]",
		NULL
	};
	static const char *group;
	bool list = false;
	const struct option rdt_options[] = {
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_STRING('g', "group", &group, "group",
		   "group to attach workload to"),
	OPT_BOOLEAN('l', "list", &list, "List resources and groups"),
	OPT_END()
	};

	if (setup_resctrl())
		return -1;

	argc = parse_options(argc, argv, rdt_options, rdt_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc && !list && !group)
		cmd_rdt_tui();

	if (list)
		return do_list();

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
