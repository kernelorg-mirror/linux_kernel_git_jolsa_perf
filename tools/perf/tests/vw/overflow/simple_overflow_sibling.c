/*
 * simple_overflow_sibling.c
 * by Vince Weaver   vweaver1 _at_ eecs.utk.edu
 *
 * Test overflow of sibling
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
#include <linux/compiler.h>

#include "perf.h"
#include "tests.h"
#include "debug.h"
#include "test_utils.h"
#include "perf_helpers.h"
#include "instructions_testcode.h"

#define MMAP_PAGES 8

static int count;

static int fd1, fd2;

static void our_handler(int signum __maybe_unused,
			siginfo_t *oh __maybe_unused,
			void *blah __maybe_unused)
{
	count++;
}


int test__vw_simple_overflow_sibling(void)
{
	size_t mmap_size = (1 + MMAP_PAGES) * page_size;
	int ret, i;
	struct perf_event_attr pe;
	struct sigaction sa;
	void *our_mmap;

	pr_debug("This tests that overflows of siblings work.\n");

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
	pe.config = PERF_COUNT_HW_CPU_CYCLES;
	pe.sample_period = 0;
	pe.sample_type = 0;
	pe.read_format = PERF_FORMAT_GROUP|PERF_FORMAT_ID;
	pe.disabled = 1;
	pe.pinned = 0;
	pe.exclude_kernel = 1;
	pe.exclude_hv = 1;
	pe.wakeup_events = 1;

	arch_adjust_domain(&pe);

	fd1 = sys_perf_event_open(&pe, 0, -1, -1, 0);
	if (fd1 < 0) {
		pr_err("Error opening leader %llx\n", pe.config);
		return TEST_FAIL;
	}

	pe.type = PERF_TYPE_HARDWARE;
	pe.config = PERF_COUNT_HW_INSTRUCTIONS;
	pe.sample_period = 100000;
	pe.sample_type = PERF_SAMPLE_IP;
	pe.read_format = 0;
	pe.disabled = 0;
	pe.pinned = 0;
	pe.exclude_kernel = 1;
	pe.exclude_hv = 1;

	arch_adjust_domain(&pe);

	fd2 = sys_perf_event_open(&pe, 0, -1, fd1, 0);
	if (fd2 < 0) {
		pr_err("Error opening %llx\n", pe.config);
		return TEST_FAIL;
	}

	/* large enough that threshold not a problem */
	our_mmap = mmap(NULL, mmap_size, PROT_READ|PROT_WRITE,
			MAP_SHARED, fd2, 0);

	fcntl(fd2, F_SETFL, O_RDWR|O_NONBLOCK|O_ASYNC);
	fcntl(fd2, F_SETSIG, SIGIO);
	fcntl(fd2, F_SETOWN, getpid());

	ioctl(fd1, PERF_EVENT_IOC_RESET, 0);
	ioctl(fd2, PERF_EVENT_IOC_RESET, 0);

	ret = ioctl(fd1, PERF_EVENT_IOC_ENABLE, 0);
	if (ret < 0) {
		pr_err("Error with PERF_EVENT_IOC_ENABLE %d %s\n",
		       errno, strerror(errno));
		return TEST_FAIL;
	}

	for (i = 0; i < 10; i++)
		instructions_million();

	ret = ioctl(fd1, PERF_EVENT_IOC_DISABLE, 0);

	pr_debug("Count: %d %p\n", count, our_mmap);

	if (count == 0) {
		pr_err("No overflow events generated.\n");
		return TEST_FAIL;
	}

	if (count != 100) {
		pr_err("Expected %d overflows, got %d.\n",
		       count, 100);
		return TEST_FAIL;
	}

	munmap(our_mmap, mmap_size);
	close(fd1);
	close(fd2);

	return TEST_OK;
}
