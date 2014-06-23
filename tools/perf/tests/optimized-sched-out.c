#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <string.h>
#include <unistd.h>
#include "tests.h"
#include "perf.h"
#include "debug.h"

static void __attribute__ ((noinline)) bp_func(void)
{
	asm("" : : : "memory");
}

static int bp_event(void *fn)
{
	struct perf_event_attr pe;
	int fd;

	memset(&pe, 0, sizeof(struct perf_event_attr));
	pe.type = PERF_TYPE_BREAKPOINT;
	pe.size = sizeof(struct perf_event_attr);

	pe.config = 0;
	pe.bp_type = HW_BREAKPOINT_X;
	pe.bp_addr = (unsigned long) fn;
	pe.bp_len = sizeof(long);

	pe.read_format = PERF_FORMAT_ID;
	pe.inherit = 1;
	pe.exclude_kernel = 1;
	pe.exclude_hv = 1;
	pe.disabled = 1;

	fd = sys_perf_event_open(&pe, 0, -1, -1, 0);
	if (fd < 0) {
		pr_debug("failed opening event %llx\n", pe.config);
		return TEST_FAIL;
	}

	return fd;
}

#define CHILDREN 100

int test__optimized_sched_out(void)
{
	struct read_data_t {
		__u64 val;
		__u64 id;
	} read_data = { 0 };
	int fd, err, i;
	__u64 id;

	fd = bp_event(bp_func);
	TEST_ASSERT_VAL("create event", fd >= 0);

	err = ioctl(fd, PERF_EVENT_IOC_ID, &id);
	TEST_ASSERT_VAL("id event", !err);

	err = ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
	TEST_ASSERT_VAL("enable event", !err);

	for (i = 0; i < CHILDREN; i++) {
		err = fork();
		TEST_ASSERT_VAL("failed to fork", err != -1);
		if (!err) {
			pr_debug("child %d created\n", getpid());
			bp_func();
			exit(0);
		}
	}

	while (wait(&err) > 0)
		;

	bp_func();

	/* read */
	err = read(fd, &read_data, sizeof(read_data));
	TEST_ASSERT_VAL("read values", sizeof(read_data) == err);
	TEST_ASSERT_VAL("event id", read_data.id  == id);

	pr_debug("read_data.val %llu, expected %u\n",
		  read_data.val, CHILDREN + 1);

	TEST_ASSERT_VAL("event count", read_data.val == CHILDREN + 1);

	close(fd);
	return 0;
}
