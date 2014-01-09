#ifndef REPORT_LOCK_H
#define REPORT_LOCK_H

#include "symbol.h"
#include "hist.h"

enum lock_mode {
	LOCK__NONE = 0,
	LOCK__CNT,
	LOCK__LIST,
};

int hist_lock_iter_cb(struct hist_entry_iter *iter,
		      struct addr_location *al,
		      bool single, void *arg);

u64 lock_hists_nr_samples(void);

int report__browse_lock_hists(struct perf_session *session, float min_percent);

int parse_lock_mode(const struct option *opt, const char *str, int unset);

int perf_lock__setup(struct perf_evlist *evlist, enum lock_mode mode);

#endif /* REPORT_LOCK_H */
