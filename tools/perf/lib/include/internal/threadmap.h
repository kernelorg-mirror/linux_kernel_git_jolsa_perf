#ifndef __LIBPERF_INTERNAL_THREADMAP_H
#define __LIBPERF_INTERNAL_THREADMAP_H

#include <linux/refcount.h>

struct thread_map_data {
	pid_t    pid;
	char	*comm;
};

struct perf_thread_map {
	refcount_t refcnt;
	int nr;
	struct thread_map_data map[];
};

#endif /* __LIBPERF_INTERNAL_THREADMAP_H */
