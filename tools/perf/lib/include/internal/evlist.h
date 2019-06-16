#ifndef __LIBPERF_INTERNAL_EVLIST_H
#define __LIBPERF_INTERNAL_EVLIST_H

struct perf_evlist {
	struct list_head         entries;
	void			*priv;
};

#endif /* __LIBPERF_INTERNAL_EVLIST_H */
