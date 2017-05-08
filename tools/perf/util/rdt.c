#include <linux/compiler.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <api/fs/fs.h>
#include "cpumap.h"
#include "util.h"
#include "rdt.h"
#include "env.h"
#include "header.h"
#include "string2.h"
#include "json.h"

static const char *rdt_name[RDT_NUM_RESOURCES] = {
	"L3", "L3DATA", "L3CODE", "L2",
};

static int rdt_name_idx(const char *res)
{
	int i;

	for (i = 0; i < RDT_NUM_RESOURCES; i++) {
		if (!strcmp(res, rdt_name[i]))
			return i;
	}

	return -1;
}

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

int rdt_dump(FILE *file)
{
	int ret;

	fprintf(file, "{\n");
	fprintf(file, "\t\"resources\" : [\n");
	ret = dump_resources(file);
	fprintf(file, "\t],\n");

	fprintf(file, "\t\"groups\" : [\n");
	ret = dump_groups(file);
	fprintf(file, "\t]\n");
	fprintf(file, "}\n");
	return ret;
}

#define EXPECT(obj, func)					\
	do {							\
		jsmntok_t *tok = data->curr;			\
		if (obj == tok->type) {				\
			if (parse_ ## func(data))		\
				goto out;			\
		} else {					\
			jsmntok_t *loc = tok;			\
			if (!tok->start && tok > data->tokens)	\
				loc = tok - 1;			\
			pr_err("rdt parse, line %d: expected " #obj ", got %s\n",	\
				json_line(data->map, loc),	\
				json_name(tok));		\
			goto out;				\
		}						\
	} while (0)

struct parse_data {
	struct rdt_data		*rdt;
	jsmntok_t		*curr;
	jsmntok_t		*tokens;
	char			*map;
	size_t			 size;
	char			*str;
	u64			 val;
	struct cpu_map		*cpus;
	struct rdt_group	 group;
	struct rdt_schemata	*schemata;
	int			 cbm_idx;
};

static int parse_string(struct parse_data *data)
{
	jsmntok_t *curr = data->curr;

	data->str = strndup(data->map + curr->start, json_len(curr));
	if (!data->str)
		return -ENOMEM;

	pr_debug("got string: %s\n", data->str);
	data->curr++;
	return 0;
}

static int parse_cpumap(struct parse_data *data)
{
	jsmntok_t *curr = data->curr;
	char *str;

	str = strndup(data->map + curr->start, json_len(curr));
	if (!str)
		return -ENOMEM;

	data->cpus = cpu_map__new(str);
	if (!data->cpus) {
		free(str);
		return -ENOMEM;
	}

	pr_debug("got cpus  : %s\n", str);
	data->curr++;
	return 0;
}

static int parse_val(struct parse_data *data, int base)
{
	jsmntok_t *curr = data->curr;
	char *str;

	str = strndup(data->map + curr->start, json_len(curr));
	if (!str)
		return -ENOMEM;

        errno = 0;
	data->val = (u64) strtoul(str, NULL, base);
	if (errno)
		return -EINVAL;

	pr_debug("got value : %lu\n", data->val);
	data->curr++;
	return 0;
}

static int parse_val_hex(struct parse_data *data)
{
	return parse_val(data, 16);
}

static int parse_val_dec(struct parse_data *data)
{
	return parse_val(data, 10);
}

static int parse_id(struct parse_data *data)
{
	int err = -1;

	data->curr += 1;
	EXPECT(JSMN_STRING, string);
	EXPECT(JSMN_STRING, string);
	err = 0;
out:
	return err;
}

static int parse_ids(struct parse_data *data)
{
	jsmntok_t *curr;
	int i, err = -1;

	curr = data->curr++;

	for (i = 0; i < curr->size; i += 1)
		EXPECT(JSMN_OBJECT, id);

	err = 0;
out:
	return err;
}

static int add_resource(struct parse_data *data, struct rdt_resource *res)
{
	struct rdt_data *rdt = data->rdt;
	int idx;

	idx = rdt_name_idx(res->name);
	if (idx < 0)
		return -EINVAL;

	rdt->resource[idx] = *res;
	return 0;
}

static int parse_resource(struct parse_data *data)
{
	struct rdt_resource res = { .enabled = true, };
	jsmntok_t *curr;
	int i, err = -1;

	curr = data->curr++;

	for (i = 0; i < curr->size; i += 2) {
		EXPECT(JSMN_STRING, string);

		if (!strcmp(data->str, "name")) {
			EXPECT(JSMN_STRING, string);
			res.name = data->str;
			data->str = NULL;
		} else if (!strcmp(data->str, "cbm_mask")) {
			EXPECT(JSMN_STRING, val_hex);
			res.cache.cbm_mask = data->val;
		} else if (!strcmp(data->str, "min_cbm_bits")) {
			EXPECT(JSMN_STRING, val_dec);
			res.cache.min_cbm_bits = data->val;
		} else if (!strcmp(data->str, "num_closids")) {
			EXPECT(JSMN_STRING, val_dec);
			res.num_closids = data->val;
		} else if (!strcmp(data->str, "ids")) {
			EXPECT(JSMN_ARRAY, ids);
		}
	}

	err = add_resource(data, &res);
out:
	return err;
}

static int parse_schemata_data_ass(struct parse_data *data)
{
	struct rdt_schemata *schemata = data->schemata;
	struct rdt_cbm *cbm = &schemata->cbm[data->cbm_idx];
	int err = -1;

	data->curr += 1;
	EXPECT(JSMN_STRING, val_dec);
	cbm->id = data->val;
	EXPECT(JSMN_STRING, val_hex);
	cbm->val = data->val;

	err = 0;
out:
	return err;
}

static int parse_schemata_data(struct parse_data *data)
{
	struct rdt_schemata *schemata = data->schemata;
	jsmntok_t *curr;
	int err = -1;

	curr = data->curr++;

	schemata->cbm = zalloc(sizeof(*schemata->cbm) * curr->size);
	if (!schemata->cbm)
		return -ENOMEM;

	schemata->cnt = curr->size;
	data->cbm_idx = 0;

	for (data->cbm_idx = 0; data->cbm_idx  < curr->size;
	     data->cbm_idx += 1)
		EXPECT(JSMN_OBJECT, schemata_data_ass);

	err = 0;
out:
	return err;
}

static int parse_schemata_name(struct parse_data *data)
{
	jsmntok_t *curr = data->curr;
	char *str;
	int idx;

	str = strndup(data->map + curr->start, json_len(curr));
	if (!str)
		return -ENOMEM;

	idx = rdt_name_idx(str);
	if (idx >= 0) {
		data->schemata = &data->group.schemata[idx];
		pr_debug("got idx   : %d\n", idx);
		data->curr++;
	}

	return idx >= 0 ? 0 : -EINVAL;
}

static int parse_schemata(struct parse_data *data __maybe_unused)
{
	jsmntok_t *curr;
	int i, err = -1;

	curr = data->curr++;

	for (i = 0; i < curr->size; i += 2) {
		EXPECT(JSMN_STRING, schemata_name);
		EXPECT(JSMN_ARRAY,  schemata_data);
	}

	err = 0;
out:
	return err;
}

static int add_group(struct parse_data *data)
{
	struct rdt_data *rdt = data->rdt;
	struct rdt_group *group;

	group = memdup(&data->group, sizeof(*group));
	if (!group)
		return -ENOMEM;

	list_add_tail(&group->list, &rdt->groups);
	return 0;
}

static int parse_group(struct parse_data *data)
{
	struct rdt_group *group = &data->group;
	jsmntok_t *curr;
	int i, err = -1;

	curr = data->curr++;

	for (i = 0; i < curr->size; i += 2) {
		EXPECT(JSMN_STRING, string);

		if (!strcmp(data->str, "name")) {
			EXPECT(JSMN_STRING, string);
			group->name = data->str;
		} else if (!strcmp(data->str, "id")) {
			EXPECT(JSMN_STRING, val_dec);
			group->id = data->val;
		} else if (!strcmp(data->str, "cpus")) {
			EXPECT(JSMN_STRING, cpumap);
			group->cpus = data->cpus;
		} else if (!strcmp(data->str, "schemata")) {
			EXPECT(JSMN_ARRAY, schemata);
		}
	}

	err = add_group(data);
out:
	return err;
}

static int parse_top_array(struct parse_data *data __maybe_unused)
{
	jsmntok_t *curr = data->curr;
	bool resources = !strcmp(data->str, "resources");
	bool groups    = !strcmp(data->str, "groups");
	int i, err = -1;

	if (!resources && !groups)
		return -1;

	data->curr += 1;

	for (i = 0; i < curr->size; i++) {
		if (resources)
			EXPECT(JSMN_OBJECT, resource);
		if (groups)
			EXPECT(JSMN_OBJECT, group);
	}

	err = 0;
out:
	return err;
}

static int parse_top(struct parse_data *data)
{
	int err = -1;

	data->curr++;
	EXPECT(JSMN_STRING, string);
	EXPECT(JSMN_ARRAY,  top_array);
	EXPECT(JSMN_STRING, string);
	EXPECT(JSMN_ARRAY,  top_array);
	err = 0;
out:
	return err;
}

static int parse(struct parse_data *data)
{
	struct rdt_data *rdt = data->rdt;
	int err = -1;

	memset(rdt, 0, sizeof(*rdt));
	INIT_LIST_HEAD(&rdt->groups);

	EXPECT(JSMN_OBJECT, top);
	err = 0;
out:
	free_json(data->map, data->size, data->tokens);
	return err;
}

int rdt_parse_map(struct rdt_data *rdt, char *map)
{
	struct parse_data data = {
		.rdt  = rdt,
		.map  = map,
		.size = strlen(map),
	};
	int len;

	data.tokens = data.curr = parse_json_map(map, data.size, &len);
	if (!data.tokens)
		return -1;

	return parse(&data);
}

int rdt_parse(struct rdt_data *rdt, char *file)
{
	struct parse_data data = {
		.rdt = rdt,
	};
	int len;

	data.tokens = data.curr = parse_json(file, &data.map, &data.size, &len);
	if (!data.tokens)
		return -1;

	return parse(&data);
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
