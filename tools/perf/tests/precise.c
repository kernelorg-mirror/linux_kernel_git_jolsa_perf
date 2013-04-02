#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "perf.h"
#include "tests.h"
#include "util.h"
#include "sysfs.h"

static int event_open_precise(int precise)
{
	struct perf_event_attr attr = {
		.type		= PERF_TYPE_HARDWARE,
		.config		= PERF_COUNT_HW_CPU_CYCLES,
		.precise_ip	= precise,
	};
	int fd;

	pr_debug("open cycles event with precise %d\n", precise);

	fd = sys_perf_event_open(&attr, 0, -1, -1, 0);
	if (fd < 0) {
		pr_debug("failed to open event, syscall returned "
			 "with %d (%s)\n", fd, strerror(errno));
		return -1;
	}

	close(fd);
	return 0;

}

int test__precise(void)
{
	int precise = perf_precise__get();
	int i;

	if (precise <= 0) {
		pr_debug("no precise info or support\n");
		return TEST_SKIP;
	}

	for (i = 1; i <= precise; i++)
		if (event_open_precise(i))
			return TEST_FAIL;

	return TEST_OK;
}
