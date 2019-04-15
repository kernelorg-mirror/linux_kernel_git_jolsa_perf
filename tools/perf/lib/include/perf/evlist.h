#ifndef __LIBPERF_EVLIST_H
#define __LIBPERF_EVLIST_H

#include <perf/core.h>

struct perf_evlist;

LIBPERF_API struct perf_evlist* perf_evlist__new(void);
LIBPERF_API void perf_evlist__delete(struct perf_evlist *evlist);

#endif /* __LIBPERF_EVLIST_H */
