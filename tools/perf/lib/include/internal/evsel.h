#ifndef __LIBPERF_INTERNAL_EVSEL_H
#define __LIBPERF_INTERNAL_EVSEL_H

#include <linux/types.h>
#include <linux/perf_event.h>

struct perf_evsel {
	struct list_head	 node;
	void			*priv;
	struct perf_event_attr	 attr;
};

#endif /* __LIBPERF_INTERNAL_EVSEL_H */
