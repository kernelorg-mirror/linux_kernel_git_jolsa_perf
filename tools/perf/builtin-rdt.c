#include <stdio.h>
#include <linux/compiler.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include "perf.h"
#include "builtin.h"
#include "debug.h"
#include <subcmd/parse-options.h>
#include <api/fs/fs.h>
#include "rdt.h"

static const char *resctrlfs;

static int setup_resctrl(void)
{
	resctrlfs = resctrlfs__mount();
	if (!resctrlfs) {
		pr_err("failed: no resctrl fs mount found\n");
		return -1;
	}

	return 0;
}

static int dump_resource(FILE *file, const char *name, char *base)
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

		scnprintf(path, PATH_MAX, "%s/info/%s", resctrlfs, name[i]);

		if (stat(path, &st))
			continue;

		dump_resource(file, name[i], path);
	}

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

	fprintf(file, "	}");

	fclose(schemata);
	return 0;
}

static int dump_group(FILE *file, const char *name, const char *base)
{
	fprintf(file, "group %s {\n", name);

	dump_cpus(file, base);
	dump_schemata(file, base);

	fprintf(file, "}\n");
	return 0;
}

static int dump_groups(FILE *file)
{
	struct dirent *entry;
	DIR *dir;

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

static int perf_rdt__dump(int argc, const char **argv)
{
	bool close_file = false;
	FILE *file = stdout;
	int ret;

	if (setup_resctrl())
		return -1;

	if (argc == 2) {
		file = fopen(argv[1], "w+");
		if (!file)
			return -1;
		close_file = true;
	}

	ret = dump_resources(file) ||
	      dump_groups(file);

	if (close_file)
		fclose(file);
	return ret;
}

int cmd_rdt(int argc, const char **argv)
{
	const char * const rdt_usage[] = {
		"perf rdt [<options>] <command>",
		"perf rdt [<options>] -- <command> [<options>]",
		NULL
	};
	const struct option rdt_options[] = {
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_END()
	};

	argc = parse_options(argc, argv, rdt_options, rdt_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
        if (!argc)
                usage_with_options(rdt_usage, rdt_options);

	if (!strncmp(argv[0], "dump", 4)) {
		return perf_rdt__dump(argc, argv);
	} else {
                usage_with_options(rdt_usage, rdt_options);
	}

	return 0;
}
