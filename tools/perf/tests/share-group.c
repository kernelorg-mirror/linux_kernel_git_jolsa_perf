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

static int create_event(void *fn, int share_fd, int group_fd, int period)
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

	if (share_fd)
		pe.disabled = 1;

	pe.exclude_kernel = 1;
	pe.exclude_hv = 1;

	pe.group_share_fd = share_fd;

	pe.read_format = PERF_FORMAT_ID|PERF_FORMAT_GROUP;

	fd = sys_perf_event_open(&pe, 0, -1, group_fd, 0);
	if (fd < 0) {
		pr_debug("failed opening event %llx\n", pe.config);
		return TEST_FAIL;
	}

	return fd;
}

#define LEADER(f, g, p) create_event(f, 1, g, p)
#define EVENT(f, g, p)  create_event(f, 0, g, p)

static int test_read(void)
{
	int fd;
	int gid_event1;
	int gid_event2;
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

	fd = LEADER(bp_leader, -1, 0);
	TEST_ASSERT_VAL("create leader", fd >= 0);

	gid_event1 = EVENT(bp_event1, fd, 0);
	TEST_ASSERT_VAL("create event1", gid_event1 >= 0);

	gid_event2 = EVENT(bp_event2, fd, 0);
	TEST_ASSERT_VAL("create event2", gid_event2 >= 0);

	id_leader = 0;
	id_event1 = gid_event1;
	id_event2 = gid_event2;

	err = ioctl(fd, PERF_EVENT_IOC_ID, &id_leader);
	TEST_ASSERT_VAL("get ID for leader", !err);

	err = ioctl(fd, PERF_EVENT_IOC_ID, &id_event1);
	TEST_ASSERT_VAL("get ID for event 1", !err);

	err = ioctl(fd, PERF_EVENT_IOC_ID, &id_event2);
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
	int gid_event1;
	int gid_event2;
	__u64 id_leader;
	__u64 id_event1;
	__u64 id_event2;
	int err;
	char *buf, *data;
	int mmap_len = page_size*2;

	fd = LEADER(bp_leader, -1, 1);
	TEST_ASSERT_VAL("create leader", fd >= 0);

	gid_event1 = EVENT(bp_event1, fd, 1);
	TEST_ASSERT_VAL("create event1", gid_event1 >= 0);

	gid_event2 = EVENT(bp_event2, fd, 1);
	TEST_ASSERT_VAL("create event2", gid_event2 >= 0);

	id_leader = 0;
	id_event1 = gid_event1;
	id_event2 = gid_event2;

	err = ioctl(fd, PERF_EVENT_IOC_ID, &id_leader);
	TEST_ASSERT_VAL("get ID for leader", !err);

	err = ioctl(fd, PERF_EVENT_IOC_ID, &id_event1);
	TEST_ASSERT_VAL("get ID for event1", !err);

	err = ioctl(fd, PERF_EVENT_IOC_ID, &id_event2);
	TEST_ASSERT_VAL("get ID for event2", !err);

	buf = mmap(NULL, mmap_len, PROT_READ, MAP_SHARED, fd, 0);
	TEST_ASSERT_VAL("mmap leader", buf != MAP_FAILED);

	err = ioctl(fd, PERF_EVENT_IOC_GROUP_SET_OUTPUT, id_event1);
	TEST_ASSERT_VAL("mmap event1", !err);

	err = ioctl(fd, PERF_EVENT_IOC_GROUP_SET_OUTPUT, id_event2);
	TEST_ASSERT_VAL("mmap event2", !err);

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

int test__share_group(void)
{
	return test_read() || test_mmap();
}
