#ifndef __PERF_TOPOLOGY_H
#define __PERF_TOPOLOGY_H

#include <stdio.h>

struct perf_tp_cache {
	int	id;
	int	level;
	int	number_of_sets;
	int	ways_of_associativity;
	char	*type;
	char	*size;
};

struct perf_tp_cpu {
	int	id;
	int	id_core;
	int 	id_socket;

	struct perf_tp_cache *caches;
	int num_caches;
};

struct perf_tp {
	struct perf_tp_cpu *cpus;
	int num_cpus;
};

int perf_tp__init(struct perf_tp *tp);
void perf_tp__clean(struct perf_tp *tp);

int perf_tp__fprintf(FILE *file, struct perf_tp *tp);

#if 0
for_each_socket(tp, socket)
	for_each_core(tp, socket, core)
		for_each_thread(tp, socket, core, cpu)

for_each_cpu(tp, cpu)

for_each_socket_cpu(tp, socket, cpu)
for_each_core_cpu(tp, core, cpu)

struct perf_tp_cpu *perf_tp__get_cpu(int cpu)

#endif

#endif /* __PERF_TOPOLOGY_H */
