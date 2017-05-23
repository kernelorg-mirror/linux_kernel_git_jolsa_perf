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
#include "thread_map.h"
#include "rdt.h"

static const char *rdt_resource_name[RDT_NUM_RESOURCES] = {
	"L3", "L3DATA", "L3CODE", "L2", "MB",
};

static const char *rdt_name(unsigned idx)
{
	return idx < RDT_NUM_RESOURCES ? rdt_resource_name[idx] : NULL;
}

static int rdt_index(const char *name)
{
	int i;

	for (i = 0; i < RDT_NUM_RESOURCES; i++) {
		if (!strcmp(rdt_resource_name[i], name))
			return i;
	}

	return -1;
}

struct perf_rdt_tool {
	struct perf_tool  tool;
	struct rdt_data	 *data;
};

void rdt_data__init(struct rdt_data *data)
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
	struct rdt_group *group;

	switch (id->type) {
	case PERF_RDT_ID_TYPE__GROUP_NAME: {
		struct rdt_group_name *data = (struct rdt_group_name*) event->rdt.data;

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
	case PERF_RDT_ID_TYPE__GROUP_CPUS: {
		struct cpu_map_data *cpus = (struct cpu_map_data *) event->rdt.data;

		group = rdt_group__findnew(rdt, id->val);
		if (!group) {
			return -ENOMEM;
		}

		if (WARN_ONCE(group->cpus, "corrupted RDT info"))
			return -EINVAL;

		group->cpus = cpu_map__new_data(cpus);
		if (!group->cpus)
			return -EINVAL;
		break;
	}
	case PERF_RDT_ID_TYPE__GROUP_SCHEMATA: {
		struct rdt_group_schemata *data = (struct rdt_group_schemata*) event->rdt.data;
		struct rdt_schemata *schemata;
		struct rdt_cbm *cbm;
		unsigned int i;

		group = rdt_group__findnew(rdt, id->val);
		if (!group) {
			return -ENOMEM;
		}

		if (WARN_ONCE(data->id >= RDT_NUM_RESOURCES, "corrupted RDT data"))
			return -EINVAL;

		schemata = &group->schemata[data->id];

		if (WARN_ONCE(schemata->cbm, "corrupted RDT data"))
			return -EINVAL;

		cbm = zalloc(sizeof(*cbm) * data->cnt);
		if (!cbm)
			return -ENOMEM;

		for (i = 0; i < data->cnt; i++) {
			cbm[i].id  = data->cbm[i].id;
			cbm[i].val = data->cbm[i].val;
		}

		schemata->cbm = cbm;
		schemata->cnt = data->cnt;
		break;
	}
	case PERF_RDT_ID_TYPE__GROUP_TASKS: {
		struct thread_map_data *data = (struct thread_map_data *) event->rdt.data;
		struct thread_map *threads;

		group = rdt_group__findnew(rdt, id->val);
		if (!group) {
			return -ENOMEM;
		}

		if (WARN_ONCE(group->threads, "corrupted RDT info"))
			return -EINVAL;

		threads = thread_map__new_event(data);
		if (!threads)
			return -EINVAL;

		group->threads = threads;
		break;
	}
	case PERF_RDT_ID_TYPE__RESOURCE_CACHE: {
		struct rdt_resource_cache *data = (struct rdt_resource_cache *) event->rdt.data;
		struct rdt_resource *res;

		if (WARN_ONCE(id->val >= RDT_NUM_RESOURCES, "corrupted RDT info"))
			return -EINVAL;

		res = &rdt->resource[id->val];

		if (WARN_ONCE(res->enabled, "corrupted RDT info"))
			return -EINVAL;

		res->enabled		= true;
		res->name		= rdt_name(id->val);
		res->num_closids	= data->num_closids;
		res->cache.cbm_mask	= data->cbm_mask;
		res->cache.min_cbm_bits	= data->min_cbm_bits;
		break;
	}
	case PERF_RDT_ID_TYPE__RESOURCE_MBA: {
		struct rdt_resource_membw *data = (struct rdt_resource_membw *) event->rdt.data;
		struct rdt_resource *res;

		if (WARN_ONCE(id->val >= RDT_NUM_RESOURCES, "corrupted RDT info"))
			return -EINVAL;

		res = &rdt->resource[id->val];

		if (WARN_ONCE(res->enabled, "corrupted RDT info"))
			return -EINVAL;

		res->enabled		  = true;
		res->name		  = rdt_name(id->val);
		res->num_closids	  = data->num_closids;
		res->membw.bandwidth_gran = data->bandwidth_gran;
		res->membw.delay_linear	  = data->delay_linear;
		res->membw.min_bandwidth  = data->min_bandwidth;
		break;
	}
	default:
		break;
	}

	return 0;
}

int perf_event__process_rdt(struct perf_tool *tool __maybe_unused,
			    union perf_event *event,
			    struct perf_session *session)
{
	struct machine *machine = &session->machines.host;
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

#define RDT_EVENT_SIZE (0xff00)

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

static int resource_id(struct rdt_id *id, u32 rid)
{
	id->val = rid;
	return 0;
}

static struct cpu_map *group_cpus(const char *base)
{
	struct cpu_map *map = NULL;
	char path[PATH_MAX];
	FILE *file;

	scnprintf(path, PATH_MAX, "%s/cpus", base);

	file = fopen(path, "r");
	if (file)
		map = cpu_map__read(file);

	fclose(file);
	return map;
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
	union perf_event *event;
	struct cpu_map *map;
	size_t size;
	int err = -EINVAL, max;
	u16 type;

	map = group_cpus(base);
	if (!map)
		return -EINVAL;

	size = sizeof(struct rdt_event) +
	       sizeof(struct cpu_map_data);

	event = cpu_map_data__alloc(map, &size, &type, &max);
	if (!event)
		goto out_map;

	event->rdt.id      = s->event->rdt.id;
	event->rdt.id.type = PERF_RDT_ID_TYPE__GROUP_CPUS;
	event->rdt.header.size = size;
	event->rdt.header.type = PERF_RECORD_RDT;

	cpu_map_data__synthesize((struct cpu_map_data *) event->rdt.data,
				 map, type, max);
	err = s->process(s->tool, event, NULL, NULL);
	free(event);
out_map:
	free(map);
	return err;
}

static int synthesize_group_tasks(struct synth *s, const char *base)
{
	union perf_event *event = s->event;
	struct thread_map_data *data = (struct thread_map_data *) event->rdt.data;
	struct thread_map *threads;
	char path[PATH_MAX];
	int ret = -EINVAL;
	u16 size;

	scnprintf(path, PATH_MAX, "%s/tasks", base);

	threads = thread_map__new_file(path);
	if (!threads)
		return -1;

	size  = sizeof(struct rdt_event);
	size += sizeof(struct thread_map_data);
	size += sizeof(data->entries[0]) * threads->nr;

	if (WARN_ONCE(size >= RDT_EVENT_SIZE,
		      "tasks crossed the event size limit"))
		goto out;

	thread_map_data__synthesize(data, threads);
	event->rdt.header.size = size;
	event->rdt.id.type     = PERF_RDT_ID_TYPE__GROUP_TASKS;
	ret = s->process(s->tool, event, NULL, NULL);
out:
	thread_map__put(threads);
	return ret;
}

static int synthesize_resource_cbm(struct synth *s, char *name,
				   struct rdt_group_cbm *cbm, int cnt)
{
	union perf_event *event = s->event;
	struct rdt_group_schemata *data = (struct rdt_group_schemata *) event->rdt.data;
	int id;
	u16 size;

	size  = sizeof(struct rdt_event);
	size += sizeof(struct rdt_group_schemata);
	size += sizeof(*cbm) * cnt;

	if (WARN_ONCE(size >= RDT_EVENT_SIZE,
		      "cbm crossed the event size limit"))
		return -EINVAL;

	id = rdt_index(name);
	if (id < 0)
		return -EINVAL;

	data->id  = id;
	data->cnt = cnt;
	memcpy(data->cbm, cbm, sizeof(*cbm) * cnt);

	event->rdt.header.size = size;
	event->rdt.id.type     = PERF_RDT_ID_TYPE__GROUP_SCHEMATA;
	return s->process(s->tool, event, NULL, NULL);
}

static int group_schemata_line(struct synth *s, char *line)
{
#define CBM_MAX 500
	struct rdt_group_cbm cbm[CBM_MAX];
	int cnt = 0, base = 16;
	char *p, *next = NULL, *name;

	line = name = trim(line);

	p = strchr(line, ':');
	if (!p)
		return -1;

	*p++ = 0;

	if (!strcmp(name, "MB"))
		base = 10;

	p = strtok_r(p, ";", &next);
	while (p) {
		char *t;

		t = strchr(p, '=');
		if (!t)
			return -1;

		*t++ = 0;

		cbm[cnt].id  = strtoull(p, NULL, 10);
		cbm[cnt].val = strtoull(t, NULL, base);

		cnt++;
		p = strtok_r(NULL, ";", &next);
	}

	return synthesize_resource_cbm(s, name, cbm, cnt);
}

static int synthesize_group_schemata(struct synth *s, const char *base)
{
	char path[PATH_MAX], buf[1000];
	FILE *schemata;

	scnprintf(path, PATH_MAX, "%s/schemata", base);
	schemata = fopen(path, "r");
	if (!schemata)
		return -EINVAL;

	while (fgets(buf, sizeof(buf), schemata)) {
		group_schemata_line(s, buf);
	};

	fclose(schemata);
	return 0;
}

static int synthesize_group(struct synth *s, const char *name, const char *base)
{
	union perf_event *event = s->event;

	if (group_id(&event->rdt.id, base))
		return -1;

	pr_debug("synthesize group %s\n", name);

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

static int get_cache(struct rdt_resource_cache *cache,
		     const char *base)
{
	unsigned long long val;
	char path[PATH_MAX];

	scnprintf(path, PATH_MAX, "%s/cbm_mask", base);
	if (filename__read_xll(path, &val)) {
		pr_err("failed: read cbm_mask for %s\n", base);
		return -1;
	}

	cache->cbm_mask = val;

	scnprintf(path, PATH_MAX, "%s/min_cbm_bits", base);
	if (filename__read_ull(path, &val)) {
		pr_err("failed: read min_cbm_bits for %s\n", base);
		return -1;
	}

	cache->min_cbm_bits = val;

	scnprintf(path, PATH_MAX, "%s/num_closids", base);
	if (filename__read_ull(path, &val)) {
		pr_err("failed: read num_closids for %s\n", base);
		return -1;
	}

	cache->num_closids = val;
	return 0;
}

static int get_membw(struct rdt_resource_membw *cache,
		     const char *base)
{
	unsigned long long val;
	char path[PATH_MAX];

	scnprintf(path, PATH_MAX, "%s/bandwidth_gran", base);
	if (filename__read_ull(path, &val)) {
		pr_err("failed: read bandwidth_gran for %s\n", base);
		return -1;
	}

	cache->bandwidth_gran = val;

	scnprintf(path, PATH_MAX, "%s/delay_linear", base);
	if (filename__read_ull(path, &val)) {
		pr_err("failed: read delay_linear for %s\n", base);
		return -1;
	}

	cache->delay_linear = val;

	scnprintf(path, PATH_MAX, "%s/min_bandwidth", base);
	if (filename__read_ull(path, &val)) {
		pr_err("failed: read min_bandwidth for %s\n", base);
		return -1;
	}

	cache->min_bandwidth = val;

	scnprintf(path, PATH_MAX, "%s/num_closids", base);
	if (filename__read_ull(path, &val)) {
		pr_err("failed: read num_closids for %s\n", base);
		return -1;
	}

	cache->num_closids = val;
	return 0;
}

static int synthesize_resource(struct synth *s, const char *base, int rid)
{
	union perf_event *event = s->event;
	u16 size;
	u32 type;

	size  = sizeof(struct rdt_event);
	size += sizeof(struct rdt_resource_cache);

	if (WARN_ONCE(size >= RDT_EVENT_SIZE,
		      "cbm crossed the event size limit"))
		return -EINVAL;

	if (rid == RDT_RESOURCE_MBA) {
		if (get_membw((struct rdt_resource_membw *) event->rdt.data, base))
			return -1;
		type = PERF_RDT_ID_TYPE__RESOURCE_MBA;
	} else {
		if (get_cache((struct rdt_resource_cache *) event->rdt.data, base))
			return -1;
		type = PERF_RDT_ID_TYPE__RESOURCE_CACHE;
	}

	resource_id(&event->rdt.id, rid);

	event->rdt.header.size = size;
	event->rdt.id.type     = type;
	return s->process(s->tool, event, NULL, NULL);
}

static int synthesize_resources(struct synth *s)
{
	int i;

	for (i = 0; i < RDT_NUM_RESOURCES; i++) {
		char path[PATH_MAX];
		struct stat st;

		scnprintf(path, PATH_MAX, "%s/info/%s",
			  s->resctrlfs, rdt_name(i));

		if (stat(path, &st))
			continue;

		if (synthesize_resource(s, path, i))
			return -1;
	}

	return 0;
}

int
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

int rdt_load(struct rdt_data *data, const char *resctrl)
{
	struct perf_rdt_tool rdt_tool = {
		.data = data,
	};

	rdt_data__init(data);
	return perf_event__synthesize_rdt(&rdt_tool.tool,
					  process_rdt_load,
					  resctrl);
}

#define P(fmt, ...)				\
	fprintf(file, hash ? "# " : "");	\
	fprintf(file, fmt, ##__VA_ARGS__);


static int display_resource_cache(FILE *file, struct rdt_resource *res,
				  bool hash)
{
	P("    cbm_mask       = %lx\n", res->cache.cbm_mask);
	P("    min_cbm_bits   = %lx\n", res->cache.min_cbm_bits);
	P("    num_closids    = %lu\n", res->num_closids);
	return 0;
}

static int display_resource_membw(FILE *file, struct rdt_resource *res,
				  bool hash)
{
	P("    bandwidth_gran = %lu\n", res->membw.bandwidth_gran);
	P("    delay_linear   = %lu\n", res->membw.delay_linear);
	P("    min_bandwidth  = %lu\n", res->membw.min_bandwidth);
	P("    num_closids    = %lu\n", res->num_closids);
	return 0;
}

static int display_group(FILE *file, struct rdt_group *group, bool hash)
{
	char buf[1000];
	int i;

	P("    id       = %d\n", group->id);

	cpu_map__snprint(group->cpus, buf, sizeof(buf));
	P("    cpus     = %s\n", buf);

	thread_map__snprint(group->threads, buf, sizeof(buf));
	P("    tasks    = %s\n", buf);

	P("    schemata { \n");

	for (i = 0; i < RDT_NUM_RESOURCES; i++) {
		struct rdt_schemata *schemata = &group->schemata[i];
		int j;

		if (!schemata->cnt)
			continue;

		P("      %s {\n", rdt_name(i));

		for (j = 0; j < schemata->cnt; j++) {
			P("        %3lu=%lx\n", schemata->cbm[j].id, schemata->cbm[j].val);
		}
		P("      }\n");
	}

	P("    }\n");
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
		if (i == RDT_RESOURCE_MBA)
			display_resource_membw(file, res, hash);
		else
			display_resource_cache(file, res, hash);
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

	ret = rdt_load(&data, resctrlfs__mount());
	if (!ret)
		ret = rdt_display(file, &data, false);

	return ret;
}
