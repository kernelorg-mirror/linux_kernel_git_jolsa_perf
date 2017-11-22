/*
 * Powerpc needs __SANE_USERSPACE_TYPES__ before <linux/types.h> to select
 * 'int-ll64.h' and avoid compile warnings when printing __u64 with %llu.
 */
#define __SANE_USERSPACE_TYPES__

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <linux/compiler.h>
#include <linux/hw_breakpoint.h>

#include "tests.h"
#include "debug.h"
#include "perf.h"
#include "cloexec.h"

static int fd1;
static int fd2;
static int fd3;
static int fd4;
static int fd5;

volatile long the_var1;
volatile long the_var2;
volatile long the_var3;
volatile long the_var5;

static noinline int test_function(void)
{
	return 0;
}

static int __event(bool is_x, void *addr)
{
	struct perf_event_attr pe;
	int fd;

	memset(&pe, 0, sizeof(struct perf_event_attr));
	pe.type = PERF_TYPE_BREAKPOINT;
	pe.size = sizeof(struct perf_event_attr);

	pe.config = 0;
	pe.bp_type = is_x ? HW_BREAKPOINT_X : HW_BREAKPOINT_W;
	pe.bp_addr = (unsigned long) addr;
	pe.bp_len = sizeof(long);

	pe.sample_period = 1;
	pe.sample_type = PERF_SAMPLE_IP;

	pe.exclude_kernel = 1;
	pe.exclude_hv = 1;

	fd = sys_perf_event_open(&pe, 0, -1, -1,
				 perf_event_open_cloexec_flag());
	if (fd < 0) {
		pr_debug("failed opening event %llx\n", pe.config);
		return TEST_FAIL;
	}

	return fd;
}

static int bp_event(void *addr)
{
	return __event(true, addr);
}

static int wp_event(void *addr)
{
	return __event(false, addr);
}

int test__bp_accounting(struct test *test __maybe_unused, int subtest __maybe_unused)
{
	fd1 = wp_event((void *)&the_var1);
	TEST_ASSERT_VAL("failed to create bp1\n", fd1 == -1);

	fd2 = wp_event((void *)&the_var2);
	TEST_ASSERT_VAL("failed to create bp2\n", fd2 == -1);

	fd3 = wp_event((void *)&the_var3);
	TEST_ASSERT_VAL("failed to create bp3\n", fd3 == -1);

	fd4 = bp_event(test_function);
	TEST_ASSERT_VAL("failed to create bp4\n", fd4 == -1);
	close(fd4);

	fd5 = wp_event((void *)&the_var5);
	TEST_ASSERT_VAL("failed to create bp5\n", fd5 == -1);

	close(fd1);
	close(fd2);
	close(fd3);
	close(fd5);
	return 0;
}
