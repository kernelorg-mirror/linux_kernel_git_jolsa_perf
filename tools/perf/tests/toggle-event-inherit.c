
#include <unistd.h>
#include <traceevent/event-parse.h>
#include "thread_map.h"
#include "evsel.h"
#include "debug.h"
#include "tests.h"

/*
 * This test creates following events:
 *
 * 1) tracepoint sys_enter_openat
 * 2) tracepoint sys_enter_close
 * 3) HW event instruction
 *
 * Events 1) and 2) are set to toggle ON and OFF
 * respectively event 3).
 * All events are created with inherit flag set.
 *
 * Workload executes test__toggle_event_raw_arch
 * first in parent and then in the child. After
 * the child is finished, we check we got 10
 * instructions instead of 5, that means plus extra
 * 5 from the child.
 *
 * Workload in test__toggle_event_raw_arch:
 *  - executes open_at syscall
 *  - executes 5 instructions
 *  - executes close syscall
 *
 * We read instruction event to validate
 * we counted 5 instructions.
 *
 */

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
int test__toggle_event_inherit(void)
{
	pr_err("The toggle event test not implemented for arch.\n");
	return 0;
}
#else
int test__toggle_event_inherit(void)
{
	struct perf_event_attr attr_on = {
		.type		= PERF_TYPE_TRACEPOINT,
		.config		= get_tp_id("sys_enter_openat"),
		.sample_period	= 1,
		.inherit	= 1,
	};
	struct perf_event_attr attr_off = {
		.type		= PERF_TYPE_TRACEPOINT,
		.config		= get_tp_id("sys_enter_close"),
		.sample_period	= 1,
		.inherit	= 1,
	};
	struct perf_event_attr attr_instr = {
		.type		= PERF_TYPE_HARDWARE,
		.config		= PERF_COUNT_HW_INSTRUCTIONS,
		.paused		= 1,
		.exclude_kernel = 1,
		.exclude_hv	= 1,
		.inherit	= 1,
	};
	int fd_on, fd_off, fd_instr;
	__u64 value, instr;
	int err, status;

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

	err = fork();
	if (err == 0) {
		test__toggle_event_raw_arch();
		_exit(0);
	} else if (err < 0) {
		pr_err("fork failed\n");
		return -1;
	}

	waitpid(-1, &status, 0);

	close(fd_on);
	close(fd_off);

	if (sizeof(value) != read(fd_instr, &value, sizeof(value)))
		pr_err("failed to read instruction event, errno %d\n", errno);

	instr *= 2;

	pr_debug("got count %llu vs %llu\n", value, instr);

	close(fd_instr);
	return instr != value;
}
#endif /* __x86_64__ */
