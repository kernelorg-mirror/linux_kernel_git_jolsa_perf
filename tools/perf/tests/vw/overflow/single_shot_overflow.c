/*
 * single_shot_overflow.c
 * by Vince Weaver   vincent.weaver _at_ maine.edu
 * Test single-shot overflow
 *
 * ported to perf by Jiri Olsa <jolsa@redhat.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/unistd.h>
#include <sys/prctl.h>
#include <linux/perf_event.h>
#include <linux/compiler.h>

#include "tests.h"
#include "test_utils.h"
#include "perf_helpers.h"
#include "instructions_testcode.h"
#include "perf.h"
#include "debug.h"

#define MMAP_PAGES 8

static struct signal_counts {
	int in, out, msg, err, pri, hup, unknown, total;
} count = { 0, 0, 0, 0, 0, 0, 0, 0 };

static int fd1;

static void our_handler(int signum __maybe_unused, siginfo_t *oh,
			void *blah __maybe_unused)
{
	switch (oh->si_code) {
	case POLL_IN:
		count.in++;
		break;
	case POLL_OUT:
		count.out++;
		break;
	case POLL_MSG:
		count.msg++;
		break;
	case POLL_ERR:
		count.err++;
		break;
	case POLL_PRI:
		count.pri++;
		break;
	case POLL_HUP:
		count.hup++;
		break;
	default:
		count.unknown++;
		break;
	}

	count.total++;

	ioctl(fd1, PERF_EVENT_IOC_REFRESH, 1);
}

int test__vw_single_shot_overflow(void)
{
	size_t mmap_size = (1 + MMAP_PAGES) * page_size;
	int ret;
	struct perf_event_attr pe;
	struct sigaction sa;
	void *our_mmap;

	pr_debug("This tests single-shot overflow.\n");

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_sigaction = our_handler;
	sa.sa_flags = SA_SIGINFO;

	if (sigaction(SIGIO, &sa, NULL) < 0) {
		pr_err("Error setting up signal handler\n");
		return TEST_FAIL;
	}

	memset(&pe, 0, sizeof(struct perf_event_attr));
	pe.type = PERF_TYPE_HARDWARE;
	pe.size = sizeof(struct perf_event_attr);
	pe.config = PERF_COUNT_HW_INSTRUCTIONS;
	pe.sample_period = 100000;
	pe.sample_type = PERF_SAMPLE_IP;
	pe.read_format = PERF_FORMAT_GROUP|PERF_FORMAT_ID;
	pe.disabled = 1;
	pe.pinned = 1;
	pe.exclude_kernel = 1;
	pe.exclude_hv = 1;

	/* not needed on 3.2?*/
	pe.wakeup_events = 1;

	arch_adjust_domain(&pe);

	fd1 = sys_perf_event_open(&pe, 0, -1, -1, 0);
	if (fd1 < 0) {
		pr_err("Error opening event %llx\n", pe.config);
		return TEST_FAIL;
	}

	our_mmap = mmap(NULL, mmap_size, PROT_READ|PROT_WRITE,
			MAP_SHARED, fd1, 0);

	fcntl(fd1, F_SETFL, O_RDWR|O_NONBLOCK|O_ASYNC);
	fcntl(fd1, F_SETSIG, SIGIO);
	fcntl(fd1, F_SETOWN, getpid());

	ioctl(fd1, PERF_EVENT_IOC_RESET, 0);

	ret = ioctl(fd1, PERF_EVENT_IOC_REFRESH, 1);
	if (ret < 0) {
		pr_err("Error with PERF_EVENT_IOC_ENABLE %d %s\n",
		       errno, strerror(errno));
		return TEST_FAIL;
	}

	instructions_million();

	munmap(our_mmap, mmap_size);
	close(fd1);

	pr_debug("Counts, using mmap buffer %p\n", our_mmap);
	pr_debug("\tPOLL_IN : %d\n", count.in);
	pr_debug("\tPOLL_OUT: %d\n", count.out);
	pr_debug("\tPOLL_MSG: %d\n", count.msg);
	pr_debug("\tPOLL_ERR: %d\n", count.err);
	pr_debug("\tPOLL_PRI: %d\n", count.pri);
	pr_debug("\tPOLL_HUP: %d\n", count.hup);
	pr_debug("\tUNKNOWN : %d\n", count.unknown);

	if (count.total == 0) {
		pr_err("No overflow events generated.\n");
		return TEST_FAIL;
	}

	if (count.in != 0) {
		pr_err("Unexpected POLL_IN interrupt.\n");
		return TEST_FAIL;
	}

	if (count.hup != 10) {
		pr_err("POLL_HUP value %d, expected %d.\n",
		       count.hup, 10);
		return TEST_FAIL;
	}

	return TEST_OK;
}
