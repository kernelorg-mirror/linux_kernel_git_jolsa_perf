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
#define CPUS_TEMPLATE_CPU       "%s/bus/event_source/devices/%s/cpus"

struct perf_pmu_format {
	char *name;
	int value;
	DECLARE_BITMAP(bits, PERF_PMU_FORMAT_BITS);
	struct list_head list;
};

struct perf_pmu_caps {
	char *name;
	char *value;
	struct list_head list;
};

struct perf_event_attr;

struct perf_pmu {
	char *name;
	char *alias_name;
	char *id;
	__u32 type;
	bool selectable;
	bool is_uncore;
	bool is_hybrid;
	bool auxtrace;
	int max_precise;
	struct perf_event_attr *default_config;
	struct perf_cpu_map *cpus;
	struct list_head format;  /* HEAD struct perf_pmu_format -> list */
	struct list_head aliases; /* HEAD struct perf_pmu_alias -> list */
	struct list_head caps;    /* HEAD struct perf_pmu_caps -> list */
	struct list_head list;    /* ELEM */
	struct list_head hybrid_list;
};

int perf_pmu__new_format(struct list_head *list, char *name,
			 int config, unsigned long *bits);
void perf_pmu__set_format(unsigned long *bits, long from, long to);
void perf_pmu_error(struct list_head *list, char *name, char const *msg);
int perf_pmu__format_parse(char *dir, struct list_head *head);

void perf_pmu__warn_invalid_config(struct perf_pmu *pmu, __u64 config,
				   const char *name);
#endif /* __LIBPERF_INTERNAL_PMU_H */
