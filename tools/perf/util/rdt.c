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
