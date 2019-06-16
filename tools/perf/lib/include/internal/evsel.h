#ifndef __LIBPERF_INTERNAL_EVSEL_H
#define __LIBPERF_INTERNAL_EVSEL_H

struct perf_evsel {
	struct list_head	 node;
	void			*priv;
};

#endif /* __LIBPERF_INTERNAL_EVSEL_H */
