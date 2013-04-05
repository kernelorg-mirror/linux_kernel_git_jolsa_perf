
#include <traceevent/event-parse.h>
#include "thread_map.h"
#include "evsel.h"
#include "debug.h"
#include "tests.h"

extern int test__toggle_event_raw_arch(void);

static int get_tp_id(const char *name)
{
	struct event_format *tp_format = event_format__new("syscalls", name);
	u64 id = 0;

	if (tp_format) {
		id = tp_format->id;
		pevent_free_format(tp_format);
	}

	return id;
}

#ifndef __x86_64__
int test__toggle_event_raw(void)
{
	pr_err("The toggle event test not implemented for arch.\n");
	return 0;
}
#else
int test__toggle_event_raw(void)
{
	struct perf_event_attr attr_on = {
		.type   = PERF_TYPE_TRACEPOINT,
		.config = get_tp_id("sys_enter_openat"),
		.sample_period = 1,
	};
	struct perf_event_attr attr_off = {
		.type   = PERF_TYPE_TRACEPOINT,
		.config = get_tp_id("sys_enter_close"),
		.sample_period = 1,
	};
	struct perf_event_attr attr_instr = {
		.type		= PERF_TYPE_HARDWARE,
		.config		= PERF_COUNT_HW_INSTRUCTIONS,
		.paused		= 1,
		.exclude_kernel = 1,
		.exclude_hv	= 1,
	};
	int fd_on, fd_off, fd_instr;
	__u64 value, instr;

	fd_instr = sys_perf_event_open(&attr_instr, 0, -1, -1, 0);
	if (fd_instr < 0) {
		pr_err("failed to open instruction event, errno %d\n", errno);
		return -1;
	}

	fd_on = sys_perf_event_open(&attr_on, 0, -1,
				    fd_instr,
				    PERF_FLAG_TOGGLE_ON);
	if (fd_on < 0) {
		pr_err("failed to open 'on' event, errno %d\n", errno);
		return -1;
	}

	fd_off = sys_perf_event_open(&attr_off, 0, -1,
				     fd_instr,
				     PERF_FLAG_TOGGLE_OFF);
	if (fd_off < 0) {
		pr_err("failed to open 'off' event, errno %d\n", errno);
		return -1;
	}

	instr = test__toggle_event_raw_arch();

	close(fd_on);
	close(fd_off);

	if (sizeof(value) != read(fd_instr, &value, sizeof(value)))
		pr_err("failed to read instruction event, errno %d\n", errno);

	pr_debug("got count %llu vs %llu\n", value, instr);

	close(fd_instr);
	return instr != value;
}
#endif /* __x86_64__ */
