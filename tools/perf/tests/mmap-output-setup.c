
#include <linux/perf_event.h>
#include <sys/mman.h>
#include <string.h>
#include "tests.h"
#include "perf.h"
#include "debug.h"

static int create_event(int group_fd)
{
	struct perf_event_attr pe;
	unsigned long flags;
	int fd;

	memset(&pe, 0, sizeof(struct perf_event_attr));
	pe.type = PERF_TYPE_SOFTWARE;
	pe.size = sizeof(struct perf_event_attr);
	pe.config = PERF_COUNT_SW_DUMMY;

	flags = group_fd != -1 ? PERF_FLAG_FD_OUTPUT : 0;

	fd = sys_perf_event_open(&pe, 0, -1, group_fd, flags);
	if (fd < 0) {
		pr_debug("failed opening event %llx\n", pe.config);
		return TEST_FAIL;
	}

	return fd;
}

int test__mmap_output_setup(void)
{
	int mmap_len = page_size*2;
	int fd_leader;
	int fd_event;
	char *buf;

	fd_leader = create_event(-1);
	TEST_ASSERT_VAL("create leader", fd_leader >= 0);

	buf = mmap(NULL, mmap_len, PROT_READ, MAP_SHARED, fd_leader, 0);
	TEST_ASSERT_VAL("mmap leader", buf != MAP_FAILED);

	fd_event = create_event(fd_leader);
	TEST_ASSERT_VAL("create event", fd_event >= 0);

	TEST_ASSERT_VAL("unmap leader", !munmap(buf, mmap_len));

	close(fd_event);
	close(fd_leader);
	return 0;
}
