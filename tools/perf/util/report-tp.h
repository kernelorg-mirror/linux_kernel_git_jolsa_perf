#ifndef PERF_REPORT_TP_H
#define PERF_REPORT_TP_H

#include "parse-options.h"

enum report_tp_mode {
	REPORT_TO_MODE__NONE,
	REPORT_TO_MODE__FIELDS,
	REPORT_TO_MODE__FORMAT,
};

int perf_evlist__add_tp_sort_entries(struct perf_evlist *evlist, enum report_tp_mode mode);

int report_tp_parse_mode(const struct option *opt,
			 const char *str, int unset);

#endif /* PERF_REPORT_TP_H */
