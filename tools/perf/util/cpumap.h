/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_CPUMAP_H
#define __PERF_CPUMAP_H

#include <stdio.h>
#include <stdbool.h>
#include <linux/refcount.h>
#include <perf/cpumap.h>
#include "perf.h"
#include "util/debug.h"

struct perf_cpu_map *cpu_map__new_data(struct cpu_map_data *data);
size_t cpu_map__fprintf(struct perf_cpu_map *map, FILE *fp);
int cpu_map__get_socket_id(int cpu);
int cpu_map__get_socket(struct perf_cpu_map *map, int idx, void *data);
int cpu_map__get_die_id(int cpu);
int cpu_map__get_die(struct perf_cpu_map *map, int idx, void *data);
int cpu_map__get_core_id(int cpu);
int cpu_map__get_core(struct perf_cpu_map *map, int idx, void *data);
int cpu_map__build_socket_map(struct perf_cpu_map *cpus, struct perf_cpu_map **sockp);
int cpu_map__build_die_map(struct perf_cpu_map *cpus, struct perf_cpu_map **diep);
int cpu_map__build_core_map(struct perf_cpu_map *cpus, struct perf_cpu_map **corep);
const struct perf_cpu_map *cpu_map__online(void); /* thread unsafe */

static inline int cpu_map__id_to_socket(int id)
{
	return id >> 24;
}

static inline int cpu_map__id_to_die(int id)
{
	return (id >> 16) & 0xff;
}

static inline int cpu_map__id_to_cpu(int id)
{
	return id & 0xffff;
}

int cpu__setup_cpunode_map(void);

int cpu__max_node(void);
int cpu__max_cpu(void);
int cpu__max_present_cpu(void);
int cpu__get_node(int cpu);

#endif /* __PERF_CPUMAP_H */
