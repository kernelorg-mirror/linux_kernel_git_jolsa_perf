#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <api/fs/fs.h>
#include "mem-events.h"
#include "debug.h"

#define E(t, n, s) { .tag = t, .name = n, .sysfs_name = s }

struct perf_mem_event perf_mem_events[PERF_MEM_EVENTS__MAX] = {
	E("ldlat-loads",	"cpu/mem-loads,ldlat=30/P",	"mem-loads"),
	E("ldlat-stores",	"cpu/mem-stores/P",		"mem-stores"),
	E("stlb-miss-loads",	"cpu/mem-stlb-miss-loads/P",	"mem-stlb-miss-loads"),
	E("stlb-miss-stores",	"cpu/mem-stlb-miss-stores/P",	"mem-stlb-miss-stores"),
	E("lock-loads",		"cpu/mem-lock-loads/P",		"mem-lock-loads"),
	E("split-loads",	"cpu/mem-split-loads/P",	"mem-split-loads"),
	E("split-stores",	"cpu/mem-split-stores/P",	"mem-split-stores"),
	E("all-loads",		"cpu/mem-all-loads/P",		"mem-all-loads"),
	E("all-stores",		"cpu/mem-all-stores/P",		"mem-all-stores"),
	E("l1-hit",		"cpu/mem-load-l1-hit/P",	"mem-load-l1-hit"),
	E("l2-hit",		"cpu/mem-load-l2-hit/P",	"mem-load-l2-hit"),
	E("l3-hit",		"cpu/mem-load-l3-hit/P",	"mem-load-l3-hit"),
	E("l1-miss",		"cpu/mem-load-l1-miss/P",	"mem-load-l1-miss"),
	E("l2-miss",		"cpu/mem-load-l2-miss/P",	"mem-load-l2-miss"),
	E("l3-miss",		"cpu/mem-load-l3-miss/P",	"mem-load-l3-miss"),
	E("lfb",		"cpu/mem-load-hit-lfb/P",	"mem-load-hit-lfb"),
	E("snp-miss",		"cpu/mem-snp-miss/P",		"mem-snp-miss"),
	E("snp-hit",		"cpu/mem-snp-hit/P",		"mem-snp-hit"),
	E("snp-hitm",		"cpu/mem-snp-hitm/P",		"mem-snp-hitm"),
	E("snp-none",		"cpu/mem-snp-none/P",		"mem-snp-none"),
	E("local-dram",		"cpu/mem-local-dram/P",		"mem-local-dram"),
};
#undef E

int perf_mem_events__parse(const char *str)
{
	char *tok, *saveptr = NULL;
	bool found = false;
	char *buf;
	int j;

	/* We need buffer that we know we can write to. */
	buf = malloc(strlen(str) + 1);
	if (!buf)
		return -ENOMEM;

	strcpy(buf, str);

	tok = strtok_r((char *)buf, ",", &saveptr);

	while (tok) {
		for (j = 0; j < PERF_MEM_EVENTS__MAX; j++) {
			struct perf_mem_event *e = &perf_mem_events[j];

			if (strstr(e->tag, tok))
				e->record = found = true;
		}

		tok = strtok_r(NULL, ",", &saveptr);
	}

	free(buf);

	if (found)
		return 0;

	pr_err("event '%s' not found, ", str);
	return -1;
}

int perf_mem_events__init(void)
{
	const char *mnt = sysfs__mount();
	bool found = false;
	int j;

	if (!mnt)
		return -ENOENT;

	for (j = 0; j < PERF_MEM_EVENTS__MAX; j++) {
		char path[PATH_MAX];
		struct perf_mem_event *e = &perf_mem_events[j];
		struct stat st;

		scnprintf(path, PATH_MAX, "%s/devices/cpu/events/%s",
			  mnt, e->sysfs_name);

		if (!stat(path, &st))
			e->supported = found = true;
	}

	return found ? 0 : -ENOENT;
}
