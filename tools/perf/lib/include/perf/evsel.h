#ifndef __LIBPERF_EVSEL_H
#define __LIBPERF_EVSEL_H

#include <perf/core.h>

struct perf_evsel;
struct perf_event_attr;

LIBPERF_API struct perf_evsel* perf_evsel__new(void);
LIBPERF_API struct perf_evsel* perf_evsel__new_attr(struct perf_event_attr *attr);
LIBPERF_API void perf_evsel__delete(struct perf_evsel *evsel);
LIBPERF_API void perf_evsel__set_priv(struct perf_evsel *evsel, void *priv);
LIBPERF_API void* perf_evsel__priv(struct perf_evsel *evsel);
LIBPERF_API struct perf_event_attr* perf_evsel__attr(struct perf_evsel *evsel);

#endif /* __LIBPERF_EVSEL_H */
