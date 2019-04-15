#ifndef __LIBPERF_CPUMAP_H
#define __LIBPERF_CPUMAP_H

#include <perf/core.h>

struct perf_cpu_map;

LIBPERF_API struct perf_cpu_map *perf_cpu_map__new(const char *cpu_list);
LIBPERF_API struct perf_cpu_map *perf_perf_cpu_map__empty_new(int nr);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__dummy_new(void);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__read(FILE *file);

LIBPERF_API size_t perf_cpu_map__snprint(struct perf_cpu_map *map, char *buf, size_t size);
LIBPERF_API size_t perf_cpu_map__snprint_mask(struct perf_cpu_map *map, char *buf, size_t size);

LIBPERF_API struct perf_cpu_map *perf_cpu_map__get(struct perf_cpu_map *map);
LIBPERF_API void perf_cpu_map__put(struct perf_cpu_map *map);

LIBPERF_API int perf_cpu_map__build_map(struct perf_cpu_map *cpus, struct perf_cpu_map **res,
					int (*f)(struct perf_cpu_map *map, int cpu, void *data),
					void *data);

LIBPERF_API int  perf_cpu_map__nr(const struct perf_cpu_map *map);
LIBPERF_API bool perf_cpu_map__empty(const struct perf_cpu_map *map);
LIBPERF_API void perf_cpu_map__set(struct perf_cpu_map *map, int idx, int cpu);
LIBPERF_API int  perf_cpu_map__cpu(const struct perf_cpu_map *cpus, int idx);
LIBPERF_API bool perf_cpu_map__has(struct perf_cpu_map *cpus, int cpu);
LIBPERF_API int  perf_cpu_map__idx(struct perf_cpu_map *cpus, int cpu);
#endif /* __LIBPERF_CPUMAP_H */
