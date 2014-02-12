#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/perf_event.h>
#include <linux/compiler.h>
#include <linux/hw_breakpoint.h>
#include <unistd.h>
#include <string.h>
#include "tests.h"
#include "perf.h"
#include "debug.h"

__attribute__ ((noinline)) static void bp_leader(void)
{
}

__attribute__ ((noinline)) static void bp_event1(void)
{
}

__attribute__ ((noinline)) static void bp_event2(void)
{
}

static int bp_event(void *fn, int fd_master, int group_fd, int period)
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

	if (period) {
		pe.sample_period = period;
		pe.sample_type = PERF_SAMPLE_ID;
	}

	pe.fd_master = fd_master;

	if (fd_master) {
		pe.disabled = 1;
		pe.read_format = PERF_FORMAT_ID|PERF_FORMAT_GROUP;
	}

	pe.exclude_kernel = 1;
	pe.exclude_hv = 1;

	fd = sys_perf_event_open(&pe, 0, -1, group_fd, 0);
	if (fd < 0) {
		pr_debug("failed opening event %llx, errno %d\n", pe.config, errno);
		return TEST_FAIL;
	}

	return fd;
}

static int test_read(void)
{
	int fd;
	int fd_id_event1;
	int fd_id_event2;
	__u64 id_leader;
	__u64 id_event1;
	__u64 id_event2;
	struct read_data_t {
		__u64 siblings;
		struct {
			__u64 val;
			__u64 id;
		} values[3];
	} read_data = { 0, {} };
	ssize_t ret;
	int err;

	fd = bp_event(bp_leader, 1, -1, 0);
	TEST_ASSERT_VAL("create leader", fd >= 0);

	fd_id_event1 = bp_event(bp_event1, 0, fd, 0);
	TEST_ASSERT_VAL("create event1", fd_id_event1 >= 0);

	fd_id_event2 = bp_event(bp_event2, 0, fd, 0);
	TEST_ASSERT_VAL("create event2", fd_id_event2 >= 0);

	id_leader = 0;
	id_event1 = fd_id_event1;
	id_event2 = fd_id_event2;

	err = ioctl(fd, PERF_EVENT_IOC_ID, &id_leader);
	TEST_ASSERT_VAL("get ID for leader", !err);

	err = ioctl(fd, PERF_EVENT_IOC_FD_ID, &id_event1);
	TEST_ASSERT_VAL("get ID for event 1", !err);

	err = ioctl(fd, PERF_EVENT_IOC_FD_ID, &id_event2);
	TEST_ASSERT_VAL("get ID for event 2", !err);

	err = ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
	TEST_ASSERT_VAL("enable leader", !err);

	bp_leader();
	bp_event1();
	bp_event2();

	err = ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
	TEST_ASSERT_VAL("enable leader", !err);

	ret = read(fd, &read_data, sizeof(read_data));
	TEST_ASSERT_VAL("read values", sizeof(read_data) == ret);

	TEST_ASSERT_VAL("siblings count", read_data.siblings == 3);

	TEST_ASSERT_VAL("id for leader", read_data.values[0].id == id_leader);
	TEST_ASSERT_VAL("count for leader", read_data.values[0].val == 1);

	TEST_ASSERT_VAL("id for event1", read_data.values[1].id == id_event1);
	TEST_ASSERT_VAL("count for event1", read_data.values[1].val == 1);

	TEST_ASSERT_VAL("id for event2", read_data.values[2].id == id_event2);
	TEST_ASSERT_VAL("count for event2", read_data.values[2].val == 1);

	close(fd);
	return 0;
}

static int test_mmap(void)
{
	struct sample_event *event;
	int fd;
	int fd_id_event1;
	int fd_id_event2;
	__u64 id_leader;
	__u64 id_event1;
	__u64 id_event2;
	int err;
	char *buf, *data;
	int mmap_len = page_size * 2;

	fd = bp_event(bp_leader, 1, -1, 1);
	TEST_ASSERT_VAL("create leader", fd >= 0);

	fd_id_event1 = bp_event(bp_event1, 0, fd, 1);
	TEST_ASSERT_VAL("create event1", fd_id_event1 >= 0);

	fd_id_event2 = bp_event(bp_event2, 0, fd, 1);
	TEST_ASSERT_VAL("create event2", fd_id_event2 >= 0);

	id_leader = 0;
	id_event1 = fd_id_event1;
	id_event2 = fd_id_event2;

	err = ioctl(fd, PERF_EVENT_IOC_ID, &id_leader);
	TEST_ASSERT_VAL("get ID for leader", !err);

	err = ioctl(fd, PERF_EVENT_IOC_FD_ID, &id_event1);
	TEST_ASSERT_VAL("get ID for event1", !err);

	err = ioctl(fd, PERF_EVENT_IOC_FD_ID, &id_event2);
	TEST_ASSERT_VAL("get ID for event2", !err);

	buf = mmap(NULL, mmap_len, PROT_READ, MAP_SHARED, fd, 0);
	TEST_ASSERT_VAL("mmap leader", buf != MAP_FAILED);

	err = ioctl(fd, PERF_EVENT_IOC_GROUP_OUTPUT);
	TEST_ASSERT_VAL("get ID for event2", !err);

	err = ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
	TEST_ASSERT_VAL("enable leader", !err);

	bp_leader();
	bp_event1();
	bp_event2();

	err = ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
	TEST_ASSERT_VAL("enable leader", !err);

	data = buf + page_size;

	/* leader */
	event = (struct sample_event *) data;

	TEST_ASSERT_VAL("leader type",
			event->header.type == PERF_RECORD_SAMPLE);
	TEST_ASSERT_VAL("leader size",
			event->header.size == 16);
	TEST_ASSERT_VAL("leader id",
			event->array[0] == id_leader);

	/* event1 */
	event = (struct sample_event *) (data + 16);

	TEST_ASSERT_VAL("event1 type",
			event->header.type == PERF_RECORD_SAMPLE);
	TEST_ASSERT_VAL("event1 size",
			event->header.size == 16);
	TEST_ASSERT_VAL("event1 id",
			event->array[0] == id_event1);

	/* event2 */
	event = (struct sample_event *) (data + 32);

	TEST_ASSERT_VAL("event2 type",
			event->header.type == PERF_RECORD_SAMPLE);
	TEST_ASSERT_VAL("event2 size",
			event->header.size == 16);
	TEST_ASSERT_VAL("event2 id",
			event->array[0] == id_event2);

	err = munmap(buf, mmap_len);
	TEST_ASSERT_VAL("munmap leader", !err);

	close(fd);
	return 0;
}

int test__control_group(void)
{
	return test_read() || test_mmap();
}
