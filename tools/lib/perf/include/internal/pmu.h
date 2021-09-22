/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_PMU_H
#define __LIBPERF_INTERNAL_PMU_H

#include <linux/list.h>
#include <linux/bitmap.h>

enum {
	PERF_PMU_FORMAT_VALUE_CONFIG,
	PERF_PMU_FORMAT_VALUE_CONFIG1,
	PERF_PMU_FORMAT_VALUE_CONFIG2,
};

#define PERF_PMU_FORMAT_BITS 64

struct perf_pmu_format {
	char *name;
	int value;
	DECLARE_BITMAP(bits, PERF_PMU_FORMAT_BITS);
	struct list_head list;
};

int perf_pmu__new_format(struct list_head *list, char *name,
			 int config, unsigned long *bits);
void perf_pmu__set_format(unsigned long *bits, long from, long to);
void perf_pmu_error(struct list_head *list, char *name, char const *msg);
int perf_pmu__format_parse(char *dir, struct list_head *head);

#endif /* __LIBPERF_INTERNAL_PMU_H */
