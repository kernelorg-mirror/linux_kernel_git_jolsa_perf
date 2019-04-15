#ifndef __LIBPERF_EVSEL_H
#define __LIBPERF_EVSEL_H

#include <perf/core.h>

struct perf_evsel;

LIBPERF_API struct perf_evsel* perf_evsel__new(void);
LIBPERF_API void perf_evsel__delete(struct perf_evsel *evsel);

#endif /* __LIBPERF_EVSEL_H */
