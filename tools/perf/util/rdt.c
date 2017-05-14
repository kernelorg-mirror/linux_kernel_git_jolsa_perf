#include <linux/compiler.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <api/fs/fs.h>
#include <asm/bug.h>
#include "cpumap.h"
#include "util.h"
#include "rdt.h"
#include "env.h"
#include "header.h"
#include "string2.h"
#include "machine.h"
#include "session.h"

#if 0
static const char *rdt_name[RDT_NUM_RESOURCES] = {
	"L3", "L3DATA", "L3CODE", "L2",
};

static int dump_ids(FILE *file, int res)
{
	static u32 level[RDT_NUM_RESOURCES] = {
		3, 3, 3, 2,
	};
	struct cpu_cache_level caches[1000], *c;
	u32 cnt, i;
	bool first = true;

	if (perf_build_caches(caches, 1000, &cnt))
		return 0;

	for (i = 0; i < cnt; i++) {
		c = &caches[i];

		if (c->level != level[res])
			continue;

		if (!first)
			fprintf(file, ",\n");
		else
			first = false;

		fprintf(file, "\t\t\t\t{ \"%3u\" : \"%s\" }", c->id, c->map);
	}
	fprintf(file, "\n");
	return 0;
}

#define DUMP_ASS(prefix, name, val, fmt, comma) \
	fprintf(file, prefix "\"%s\" : \"" fmt "\"%s\n", name, val, comma)

static int dump_resource(FILE *file, int res, const char *name, char *base)
{
	unsigned long long val;
	char path[PATH_MAX];

	DUMP_ASS("\t\t\t", "name", name, "%s", ",");

	scnprintf(path, PATH_MAX, "%s/cbm_mask", base);
	if (filename__read_ull(path, &val)) {
		pr_err("failed: read cbm_mask for %s\n", name);
		return -1;
	}

	DUMP_ASS("\t\t\t", "cbm_mask", val, "%llx", ",");

	scnprintf(path, PATH_MAX, "%s/min_cbm_bits", base);
	if (filename__read_ull(path, &val)) {
		pr_err("failed: read min_cbm_bits for %s\n", name);
		return -1;
	}

	DUMP_ASS("\t\t\t", "min_cbm_bits", val, "%llu", ",");

	scnprintf(path, PATH_MAX, "%s/num_closids", base);
	if (filename__read_ull(path, &val)) {
		pr_err("failed: read num_closids for %s\n", name);
		return -1;
	}

	DUMP_ASS("\t\t\t", "num_closids", val, "%llu", ",");

	fprintf(file, "\t\t\t\"ids\" : [\n");
	dump_ids(file, res);
	fprintf(file, "\t\t\t ]\n");
	return 0;
}

static int dump_resources(FILE *file)
{
	int i;

	for (i = 0; i < RDT_NUM_RESOURCES; i++) {
		char path[PATH_MAX];
		struct stat st;

		scnprintf(path, PATH_MAX, "%s/info/%s",
			  resctrlfs__mount(), rdt_name[i]);

		if (stat(path, &st))
			continue;

		if (i != 0)
			fprintf(file, "\t\t},\n");

		fprintf(file, "\t\t{\n");
		dump_resource(file, i, rdt_name[i], path);
	}

	fprintf(file, "\t\t}\n");

	return 0;
}

static int dump_id(FILE *file, const char *base)
{
	char path[PATH_MAX];
	int id, err;

	scnprintf(path, PATH_MAX, "%s/id", base);
	err = filename__read_int(path, &id);
	if (err)
		return err == ENOENT ? 0 : err;

	DUMP_ASS("\t\t\t", "id", id, "%d", ",");
	return 0;
}

static int dump_cpus(FILE *file, const char *base)
{
	struct cpu_map *map;
	char path[PATH_MAX], buf[1000];
	FILE *cpus;

	scnprintf(path, PATH_MAX, "%s/cpus", base);
	cpus = fopen(path, "r");
	if (!cpus)
		return -EINVAL;

	map = cpu_map__read(cpus);
	fclose(cpus);
	if (!map)
		return -EINVAL;

	if (!map->nr)
		return 0;

        cpu_map__snprint(map, buf, sizeof(buf));
	DUMP_ASS("\t\t\t", "cpus", buf, "%s", ",");
	cpu_map__put(map);
	return 0;
}

static int dump_schemata_line(FILE *file, char *line)
{
	char *p, *next;
	bool first = true;

	line = trim(line);

	p = strchr(line, ':');
	if (!p)
		return -1;

	*p++ = 0;

	fprintf(file, "\t\t\t\t\"%s\" : [\n", line);

	p = strtok_r(p, ";", &next);
	while (p) {
		char *t;

		if (!first)
			fprintf(file, "},\n");

		t = strchr(p, '=');
		if (!t)
			return -1;

		*t++ = 0;
		fprintf(file, "\t\t\t\t\t{ \"%3s\" : \"%s\" ", p, t);

		p = strtok_r(NULL, ";", &next);
		first = false;
	}

	fprintf(file, "}\n");
	fprintf(file, "\t\t\t\t]\n");
	return 0;
}

static int dump_schemata(FILE *file, const char *base)
{
	char path[PATH_MAX], buf[1000];
	FILE *schemata;

	scnprintf(path, PATH_MAX, "%s/schemata", base);
	schemata = fopen(path, "r");
	if (!schemata)
		return -EINVAL;

	fprintf(file, "\t\t\t\"schemata\" : [\n");

	while (fgets(buf, sizeof(buf), schemata)) {
		dump_schemata_line(file, buf);
	};

	fprintf(file, "\t\t\t]\n");

	fclose(schemata);
	return 0;
}

static int dump_group(FILE *file, const char *name, const char *base)
{
	DUMP_ASS("\t\t\t", "name", name, "%s", ",");

	dump_id(file, base);
	dump_cpus(file, base);
	dump_schemata(file, base);

	return 0;
}

static int dump_groups(FILE *file)
{
	struct dirent *entry;
	const char *resctrlfs;
	DIR *dir;

	resctrlfs = resctrlfs__mount();
	if (!resctrlfs)
		return -EINVAL;

	dir = opendir(resctrlfs);
	if (!dir)
		return -1;

	fprintf(file, "\t\t{\n");
	dump_group(file, "default", resctrlfs);

	while ((entry = readdir(dir))) {
		char path[PATH_MAX];

		if (entry->d_type != DT_DIR)
			continue;

		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0 ||
		    strcmp(entry->d_name, "info") == 0)
			continue;

		fprintf(file, "\t\t},\n");
		fprintf(file, "\t\t{\n");

		scnprintf(path, PATH_MAX, "%s/%s", resctrlfs, entry->d_name);

		dump_group(file, entry->d_name, path);
	}

	fprintf(file, "\t\t}\n");

	closedir(dir);
	return 0;
}

struct rdt_tool {
	struct perf_tool	 tool;
	struct rdt_data		 data;
	struct rdt_group	*last;
};

static int process_rdt(struct perf_tool *tool,
			union perf_event *event,
			struct perf_sample *sample,
			struct machine *machine);
{
	struct rdt_tool		*rdt_tool = container_of(tool, struct rdt_tool, tool);
	struct rdt_data		*data     = &rdt_tool->data;;
	struct rdt_id		*id       = &rdt_event->rdt.id;
	struct rdt_group	*group;

	switch (id->type) {
	case PERF_RDT_ID_TYPE__GROUP_NAME: {
		struct rdt_group_name *name = event->rdt.data;

		group = rdt_tool__group(data, id);
		if (!group)
			return -ENOMEM;

		strcpy(group->name, name->str);
		break;
	}
	case PERF_RDT_ID_TYPE__GROUP_CPUS: {
		struct rdt_group_cpus *cpus;

		group = rdt_tool__group(data, id);
		if (!group)
			return -ENOMEM;

		break;
	}
	case PERF_RDT_ID_TYPE__GROUP_TASKS:
	case PERF_RDT_ID_TYPE__GROUP_SCHEMATA:
	default:
	}
}

#endif

struct perf_rdt_tool {
	struct perf_tool  tool;
	struct rdt_data	 *data;
};

static void rdt_data__init(struct rdt_data *data)
{
	memset(data, 0, sizeof(*data));
	INIT_LIST_HEAD(&data->groups);
}

struct rdt_group *rdt_group__find(struct rdt_data *data, u32 closid)
{
	struct rdt_group *group;

	list_for_each_entry(group, &data->groups, list) {
		if ((u32) group->id == closid)
			return group;
	}

	return NULL;
}

static struct rdt_group *rdt_group__new(struct rdt_data *data, u32 closid)
{
	struct rdt_group *group;

	group = zalloc(sizeof(*group));
	if (group) {
		group->id = closid;
		list_add_tail(&group->list, &data->groups);
	}
	return group;
}

static struct rdt_group *rdt_group__findnew(struct rdt_data *data, u32 closid)
{
	struct rdt_group *group;

	group = rdt_group__find(data, closid);
	return group ?: rdt_group__new(data, closid);
}

static int process_rdt(struct rdt_data *rdt, union perf_event *event)
{
	struct rdt_id *id = &event->rdt.id;

	switch (id->type) {
	case PERF_RDT_ID_TYPE__GROUP_NAME: {
		struct rdt_group_name *data = (struct rdt_group_name*) event->rdt.data;
		struct rdt_group *group;

		group = rdt_group__findnew(rdt, id->val);
		if (!group) {
			return -ENOMEM;
		}

		if (WARN_ONCE(group->name, "corrupted RDT info"))
			return -EINVAL;

		group->name = strdup(data->name);
		if (!group->name)
			return -ENOMEM;
		break;
	}
	default:
		break;
	}

	return 0;
}

int perf_event__process_rdt(struct perf_tool *tool __maybe_unused,
			    union perf_event *event,
			    struct perf_sample *sample __maybe_unused,
			    struct machine *machine)
{
	struct rdt_data *data;

	data = &machine->env->rdt;
	return process_rdt(data, event);
}

static int process_rdt_load(struct perf_tool *tool,
			    union perf_event *event,
			    struct perf_sample *sample __maybe_unused,
			    struct machine *machine __maybe_unused)
{
	struct perf_rdt_tool *rdt_tool;

	rdt_tool = container_of(tool, struct perf_rdt_tool, tool);
	return process_rdt(rdt_tool->data, event);
}

#define RDT_EVENT_SIZE (sizeof(struct rdt_event) + PATH_MAX)

struct synth {
	struct perf_tool	*tool;
	perf_event__handler_t	 process;
	const char		*resctrlfs;
	union perf_event	*event;
};

static int group_id(struct rdt_id *id, const char *base)
{
	char path[PATH_MAX];
	int err;

	scnprintf(path, PATH_MAX, "%s/id", base);
	err = filename__read_int(path, (int *) &id->val);
	if (err)
		return err == ENOENT ? 0 : err;
	return 0;
}

static int synthesize_group_name(struct synth *s, const char *name)
{
	union perf_event *event = s->event;
	struct rdt_group_name *data = (struct rdt_group_name *) event->rdt.data;
	int len;

	len = scnprintf(data->name, PATH_MAX, "%s", name);
	len = PERF_ALIGN(len, sizeof(u64));

	event->rdt.header.size = sizeof(struct rdt_event) + len;
	event->rdt.id.type     = PERF_RDT_ID_TYPE__GROUP_NAME;
	return s->process(s->tool, event, NULL, NULL);
}

static int synthesize_group_cpus(struct synth *s, const char *base)
{
	union perf_event *event = s->event;
	struct rdt_group_cpus *data = (struct rdt_group_cpus *) event->rdt.data;
	struct cpu_map_data *cpus;

	event->rdt.header.size = sizeof(struct rdt_event) + len;
	event->rdt.id.type     = PERF_RDT_ID_TYPE__GROUP_NAME;
	return s->process(s->tool, event, NULL, NULL);
	return 0;
}

static int synthesize_group_tasks(struct synth *s __maybe_unused, const char *name __maybe_unused)
{
	return 0;
}

static int synthesize_group_schemata(struct synth *s __maybe_unused, const char *base __maybe_unused)
{
	return 0;
}

static int synthesize_group(struct synth *s, const char *name, const char *base)
{
	union perf_event *event = s->event;

	if (group_id(&event->rdt.id, base))
		return -1;

	return synthesize_group_name(s, name) ||
	       synthesize_group_cpus(s, base) ||
	       synthesize_group_tasks(s, base) ||
	       synthesize_group_schemata(s, base);
}

static int synthesize_groups(struct synth *s)
{
	struct dirent *entry;
	DIR *dir;
	int err;

	dir = opendir(s->resctrlfs);
	if (!dir)
		return -1;

	err = synthesize_group(s, "default", s->resctrlfs);

	while (!err && (entry = readdir(dir))) {
		char path[PATH_MAX];

		if (entry->d_type != DT_DIR)
			continue;

		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0 ||
		    strcmp(entry->d_name, "info") == 0)
			continue;

		scnprintf(path, PATH_MAX, "%s/%s", s->resctrlfs, entry->d_name);
		err = synthesize_group(s, entry->d_name, path);
	}

	closedir(dir);
	return err;
}

static int synthesize_resources(struct synth *s __maybe_unused)
{
	return 0;
}

static int
perf_event__synthesize_rdt(struct perf_tool *tool,
			   perf_event__handler_t process,
			   const char *resctrlfs)
{
	struct synth s = {
		.tool      = tool,
		.process   = process,
		.resctrlfs = resctrlfs,
	};
	int ret;

	s.event = zalloc(RDT_EVENT_SIZE);
	if (!s.event)
		return -ENOMEM;

	/* invariants */
	s.event->header.type    = PERF_RECORD_RDT;
	s.event->rdt.id.version = PERF_RDT_ID_VERSION_1;

	ret = synthesize_groups(&s) ||
	      synthesize_resources(&s);

	free(s.event);
	return ret;
}

static int rdt_load(struct rdt_data *data)
{
	struct perf_rdt_tool rdt_tool = {
		.data = data,
	};

	rdt_data__init(data);
	return perf_event__synthesize_rdt(&rdt_tool.tool,
					  process_rdt_load,
					  resctrlfs__mount());
}

#define P(fmt, ...)				\
	fprintf(file, hash ? "# " : "");	\
	fprintf(file, fmt, ##__VA_ARGS__);


static int display_resource(FILE *file, struct rdt_resource *res,
			    bool hash)
{
	P("    cbm_mask       = %lu\n", res->cache.cbm_mask);
	P("    min_cbm_bits   = %lu\n", res->cache.min_cbm_bits);
	P("    num_closids    = %lu\n", res->num_closids);
	return 0;
}

static int display_group(FILE *file, struct rdt_group *group, bool hash)
{
	P("    id = %d\n", group->id);
	return 0;
}

int rdt_display(FILE *file, struct rdt_data *rdt, bool hash)
{
	struct rdt_group *group;
	int i;

	P("Resources:\n");
	for (i = 0; i < RDT_NUM_RESOURCES; i++) {
		struct rdt_resource *res = &rdt->resource[i];

		if (!res->enabled)
			continue;

		P("  %s {\n", res->name);
		display_resource(file, res, hash);
		P("  }\n");
	}

	P("Groups:\n");

	list_for_each_entry(group, &rdt->groups, list) {
		P("  %s {\n", group->name);
		display_group(file, group, hash);
		P("  }\n");
	}

	return 0;
}

#undef P

int rdt_dump(FILE *file)
{
	struct rdt_data data;
	int ret;

	ret = rdt_load(&data);
	if (!ret)
		ret = rdt_display(file, &data, false);

	return ret;
}
