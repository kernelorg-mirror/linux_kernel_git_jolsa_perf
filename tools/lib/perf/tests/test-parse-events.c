#include <stdio.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <perf/evlist.h>
#include <perf/evsel.h>
#include <internal/evlist.h>
#include <internal/tests.h>
#include "tests.h"

static int libperf_print(enum libperf_print_level level,
			 const char *fmt, va_list ap)
{
	return vfprintf(stderr, fmt, ap);
}

int test_parse_events(int argc, char **argv)
{
	struct perf_evlist *evlist;
	struct perf_evsel *evsel;
	int err;

	__T_START;

	libperf_init(libperf_print);

	evlist = perf_evlist__new();
	__T("failed to create evlist", evlist);

	err = libperf_parse_events(evlist, "mem:0:rw");
	__T("failed to parse events", !err);

	evsel = perf_evlist__first(evlist);

	__T("wrong type", PERF_TYPE_BREAKPOINT == evsel->attr.type);
	__T("wrong config", 0 == evsel->attr.config);
	__T("wrong bp_type", (HW_BREAKPOINT_R|HW_BREAKPOINT_W) == evsel->attr.bp_type);
	__T("wrong bp_len", HW_BREAKPOINT_LEN_4 == evsel->attr.bp_len);

	perf_evlist__delete(evlist);

	__T_END;
	return tests_failed == 0 ? 0 : -1;
}
