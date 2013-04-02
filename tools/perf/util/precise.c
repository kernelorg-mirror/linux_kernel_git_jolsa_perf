
#include <linux/kernel.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include "sysfs.h"
#include "util.h"

/*
 * Returns maximum value allowed for
 * perf_event_attr::precise_ip, and:
 * -1 if we failed to read the sysfs attribute or
 *    sysfs attribute is not found (supported)
 */
int perf_precise__get(void)
{
	static int precise = -1;
	struct stat st;
	char path[PATH_MAX];

	if (precise != -1)
		return precise;

	scnprintf(path, PATH_MAX, "%s/devices/cpu/precise",
		  sysfs_find_mountpoint());

	if (!lstat(path, &st)) {
		FILE *file;

		file = fopen(path, "r");
		if (!file)
			return -1;

		if (1 != fscanf(file, "%d", &precise))
			pr_debug("failed to read precise info\n");

		fclose(file);
	} else
		/* Return -1 if there's no sysfs precise support. */
		return -1;

	return precise;
}
