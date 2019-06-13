#ifndef __LIBPERF_THREADMAP_H
#define __LIBPERF_THREADMAP_H

#include <sys/types.h>
#include <stdbool.h>
#include <perf/core.h>

struct perf_thread_map;

LIBPERF_API struct perf_thread_map *perf_thread_map__new_dummy(void);
LIBPERF_API struct perf_thread_map *perf_thread_map__new_by_pid(pid_t pid);
LIBPERF_API struct perf_thread_map *perf_thread_map__new_by_tid(pid_t tid);
LIBPERF_API struct perf_thread_map *perf_thread_map__new(pid_t pid, pid_t tid, uid_t uid);
LIBPERF_API struct perf_thread_map *perf_thread_map__empty_new(int nr);

LIBPERF_API struct perf_thread_map *perf_thread_map__get(struct perf_thread_map *map);
LIBPERF_API void perf_thread_map__put(struct perf_thread_map *map);

LIBPERF_API struct perf_thread_map *perf_thread_map__new_str(const char *pid,
		const char *tid, uid_t uid, bool all_threads);

LIBPERF_API struct perf_thread_map *perf_thread_map__new_by_tid_str(const char *tid_str);

LIBPERF_API int perf_thread_map__nr(const struct perf_thread_map *threads);
LIBPERF_API pid_t perf_thread_map__pid(struct perf_thread_map *map, int thread);
LIBPERF_API void perf_thread_map__set_pid(struct perf_thread_map *map, int thread, pid_t pid);
LIBPERF_API char *perf_thread_map__comm(struct perf_thread_map *map, int thread);
LIBPERF_API void perf_thread_map__set_comm(struct perf_thread_map *map, int thread, char *comm);

LIBPERF_API void perf_thread_map__read_comms(struct perf_thread_map *threads);
LIBPERF_API bool perf_thread_map__has(struct perf_thread_map *threads, pid_t pid);
LIBPERF_API int perf_thread_map__remove(struct perf_thread_map *threads, int idx);
#endif /* __LIBPERF_THREADMAP_H */
