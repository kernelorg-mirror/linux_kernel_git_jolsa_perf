
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "pmu.h"
#include "util.h"
#include "sysfs.h"

static int add_aliases(struct list_head *head, char *path, const char **files)
{
	char file[PATH_MAX];
	int ret = 0;

	while (!ret && *files) {
		scnprintf(file, PATH_MAX, "%s/%s", path, *files);
		ret = pmu_aliases_parse_multi(file, head);
		files++;
	}

	return ret;
}

#define ADD_ALIASES(...)				\
do {							\
	const char *__files[] = { __VA_ARGS__ , NULL };	\
	return add_aliases(head, path, __files);	\
} while (0)

static int get_path(const char *vendor, char *path, int size)
{
	struct stat st;

	/* try local one first */
	scnprintf(path, size, "./arch/x86/events/%s/", vendor);

	if (stat(path, &st) < 0) {
		/* and installed later */
		scnprintf(path, size, "%s/%s/events/x86/%s/",
			  PREFIX, PERF_EXEC_PATH, vendor);

		if (stat(path, &st) < 0)
			return -1;
	}

	return 0;
}

static int intel_aliases(struct list_head *head, unsigned model)
{
	char path[PATH_MAX];

	if (get_path("intel", path, PATH_MAX))
		return -1;

	switch (model) {
	default:
		ADD_ALIASES("");
		break;
	}

	return 0;
}

static int cpu_specs(unsigned *vendor, unsigned *model)
{
	FILE *file;
	struct stat st;
	char path[PATH_MAX];
	int ret = 0;

	scnprintf(path, PATH_MAX, "%s/devices/system/cpu/modalias",
		  sysfs_find_mountpoint());

	if (stat(path, &st) < 0)
		return -ENOENT;

	file = fopen(path, "r");
	if (!file)
		return -errno;

	if (2 != fscanf(file, "x86cpu:vendor:%X:family:%*X:model:%X:",
			vendor, model))
		ret = -1;

	fclose(file);
	return ret;
}

static int cpu_aliases(struct list_head *head)
{
	unsigned vendol, model;
	int ret;

	ret = cpu_specs(&vendol, &model);
	if (ret) {
		pr_info("failed to get cpu aliases");
		return 0;
	}

	switch (vendol) {
	/* Intel */
	case 0:
		return intel_aliases(head, model);
	default:
		/* unknown vendor.. plenty to cover ;-) */
		return 0;
	}

	return 0;
}

int arch_pmu_aliases(char *name, struct list_head *head)
{
	if (!strcmp(name, "cpu"))
		return cpu_aliases(head);

	return 0;
}
