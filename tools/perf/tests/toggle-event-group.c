#include <sys/types.h>
#include <unistd.h>
#include <traceevent/event-parse.h>
#include "thread_map.h"
#include "evsel.h"
#include "debug.h"
#include "tests.h"

/*
 * We want to toggle instructions on/off only after chained
 * execution of defined tracepoints, like:
 *
 * getuid();
 *    openat();
 *       instructions to count
 *    close();
 * getppid();
 *
 * This test creates following events:
 *
 * 1) tracepoint sys_enter_getuid
 * 2) tracepoint sys_enter_getppid
 * 3) tracepoint sys_enter_openat
 * 4) tracepoint sys_enter_close
 * 5) HW event instruction
 *
 *
 * Events 3) and 4) are created as a group with 3) as the leader.
 * Events 3) and 4) toggle ON and OFF respectively event 5).
 * Events 1) and 2) toggle ON and OFF respectively event 3).
 *
 * This means:
 *   - when the workload executes getuid(), the group (events 3
 *     and 4) is toggled ON.
 *   - when the workload executes close, the group (events 3
 *     and 4) is toggled OFF.
 *   - when the workload executes started events 3) and 4) they
 *     toggle ON and OFF respectively instructions event.
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
int test__toggle_event_group(void)
{
	pr_err("The toggle event test not implemented for arch.\n");
	return 0;
}
#else

static int test(void)
{
	int instr;

	getuid();
	instr = test__toggle_event_raw_arch();
	getppid();
	return instr;
}

#ifndef PERF_EVENT_IOC_SET_TOGGLE
#define PERF_EVENT_IOC_SET_TOGGLE 1074275336
#endif

static int toggle_event(int fd_event, int flag, int fd_toggled)
{
	u64 args[2] = { fd_toggled, flag };
	return ioctl(fd_event, PERF_EVENT_IOC_SET_TOGGLE, args);
}

int test__toggle_event_group(void)
{
	struct perf_event_attr attr_group_on = {
		.type   = PERF_TYPE_TRACEPOINT,
		.config = get_tp_id("sys_enter_getuid"),
		.sample_period = 1,
	};
	struct perf_event_attr attr_group_off = {
		.type   = PERF_TYPE_TRACEPOINT,
		.config = get_tp_id("sys_enter_getppid"),
		.sample_period = 1,
	};
	struct perf_event_attr attr_on = {
		.type   = PERF_TYPE_TRACEPOINT,
		.config = get_tp_id("sys_enter_openat"),
		.sample_period = 1,
		.paused		= 1,
	};
	struct perf_event_attr attr_off = {
		.type   = PERF_TYPE_TRACEPOINT,
		.config = get_tp_id("sys_enter_close"),
		.sample_period = 1,
		.paused		= 1,
	};
	struct perf_event_attr attr_instr = {
		.type		= PERF_TYPE_HARDWARE,
		.config		= PERF_COUNT_HW_INSTRUCTIONS,
		.paused		= 1,
		.exclude_kernel = 1,
		.exclude_hv	= 1,
	};
	int fd_group_on, fd_group_off, fd_on, fd_off, fd_instr;
	__u64 value, instr;

	fd_instr = sys_perf_event_open(&attr_instr, 0, -1, -1, 0);
	if (fd_instr < 0) {
		pr_err("failed to open instruction event, errno %d\n", errno);
		return -1;
	}

	fd_on = sys_perf_event_open(&attr_on, 0, -1, -1, 0);
	if (fd_on < 0) {
		pr_err("failed to open 'on' event, errno %d\n", errno);
		return -1;
	}

	fd_off = sys_perf_event_open(&attr_off, 0, -1, fd_on, 0);
	if (fd_off < 0) {
		pr_err("failed to open 'off' event, errno %d\n", errno);
		return -1;
	}

	if (toggle_event(fd_on, PERF_FLAG_TOGGLE_ON, fd_instr)) {
		pr_err("failed to set toggle 'on', errno %d\n", errno);
		return -1;
	}

	if (toggle_event(fd_off, PERF_FLAG_TOGGLE_OFF, fd_instr)) {
		pr_err("failed to set toggle 'off', errno %d\n", errno);
		return -1;
	}

	fd_group_on = sys_perf_event_open(&attr_group_on, 0, -1,
					  fd_on, PERF_FLAG_TOGGLE_ON);
	if (fd_group_on < 0) {
		pr_err("failed to open 'group_on' event, errno %d\n", errno);
		return -1;
	}

	fd_group_off = sys_perf_event_open(&attr_group_off, 0, -1,
					   fd_on, PERF_FLAG_TOGGLE_OFF);
	if (fd_group_off < 0) {
		pr_err("failed to open 'group_off' event, errno %d\n", errno);
		return -1;
	}

#define READ(i, exp)								\
do {										\
	if (sizeof(value) != read(fd_instr, &value, sizeof(value))) {		\
		pr_err("failed to read instruction event, errno %d\n", errno);	\
		return -1;							\
	}									\
	pr_debug("%d got count %llu vs %lu\n", i, value, (unsigned long) exp);	\
} while (0)


	READ(1, 0);

	test__toggle_event_raw_arch();

	READ(2, 0);

	instr = test();

	READ(3, instr);

	test__toggle_event_raw_arch();

	READ(4, instr);

	close(fd_on);
	close(fd_off);
	close(fd_group_on);
	close(fd_group_off);

	READ(5, instr);

	close(fd_instr);
	return instr != value;
}
#endif /* __x86_64__ */
