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

static int dump_ids(FILE *file, int res)
{
	static u32 level[RDT_NUM_RESOURCES] = {
		3, 3, 3, 2,
	};
	struct cpu_cache_level caches[1000], *c;
	u32 cnt, i;

	if (perf_build_caches(caches, 1000, &cnt))
		return 0;

	fprintf(file, "\tids = {\n");

	for (i = 0; i < cnt; i++) {
		c = &caches[i];

		if (c->level != level[res])
			continue;

		fprintf(file, "\t\t%4u = %s\n", c->id, c->map);
	}

	fprintf(file, "\t}\n");
	return 0;
}

static int dump_resource(FILE *file, int res, const char *name, char *base)
{
	unsigned long long val;
	char path[PATH_MAX];

	fprintf(file, "resource %s {\n", name);

	scnprintf(path, PATH_MAX, "%s/cbm_mask", base);
	if (filename__read_ull(path, &val)) {
		pr_err("failed: read cbm_mask for %s\n", name);
		return -1;
	}

	fprintf(file, "	cbm_mask     = %llx\n", val);

	scnprintf(path, PATH_MAX, "%s/min_cbm_bits", base);
	if (filename__read_ull(path, &val)) {
		pr_err("failed: read min_cbm_bits for %s\n", name);
		return -1;
	}

	fprintf(file, "	min_cbm_bits = %llu\n", val);

	scnprintf(path, PATH_MAX, "%s/num_closids", base);
	if (filename__read_ull(path, &val)) {
		pr_err("failed: read num_closids for %s\n", name);
		return -1;
	}

	fprintf(file, "	num_closids  = %llu\n", val);

	dump_ids(file, res);

	fprintf(file, "}\n");
	return 0;
}

static int dump_resources(FILE *file)
{
	static const char *name[RDT_NUM_RESOURCES] = {
		"L3", "L3DATA", "L3CODE", "L2",
	};
	int i;

	for (i = 0; i < RDT_NUM_RESOURCES; i++) {
		char path[PATH_MAX];
		struct stat st;

		scnprintf(path, PATH_MAX, "%s/info/%s", resctrlfs__mount(), name[i]);

		if (stat(path, &st))
			continue;

		dump_resource(file, i, name[i], path);
	}

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

	fprintf(file, "	id = %d\n", id);
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
	fprintf(file, "	cpus = %s\n", buf);
	cpu_map__put(map);
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

	fprintf(file, "	schemata = {\n");

	while (fgets(buf, sizeof(buf), schemata)) {
		fprintf(file, "\t\t%s", buf);
	};

	fprintf(file, "	}\n");

	fclose(schemata);
	return 0;
}

static int dump_group(FILE *file, const char *name, const char *base)
{
	fprintf(file, "group %s {\n", name);

	dump_id(file, base);
	dump_cpus(file, base);
	dump_schemata(file, base);

	fprintf(file, "}\n");
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

	dump_group(file, "default", resctrlfs);

	while ((entry = readdir(dir))) {
		char path[PATH_MAX];

		if (entry->d_type != DT_DIR)
			continue;

		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0 ||
		    strcmp(entry->d_name, "info") == 0)
			continue;

		scnprintf(path, PATH_MAX, "%s/%s", resctrlfs, entry->d_name);

		dump_group(file, entry->d_name, path);
	}

	closedir(dir);
	return 0;
}

int rdt_dump(FILE *file)
{
	return dump_resources(file) || dump_groups(file);
}
