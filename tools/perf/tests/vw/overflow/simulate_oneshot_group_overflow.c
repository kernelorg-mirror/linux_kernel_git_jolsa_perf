/*
 * simul_oneshot_group_overflow.c
 * by Vince Weaver   vincent.weaver@maine.edu
 *
 * Test to see if we can sample on two events at once
 * within the same group in oneshot fashion.
 *
 * the answer seems to be no, but it's complicated
 * this test tries a PAPI-esque way of doing things
 * in this case we disable the event in the handler
 * but in the group case if it's the group leader
 * everything gets disabled, but refresh only
 * restarts the one event.
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
#include "matrix_multiply.h"

#define MMAP_PAGES 8
#define NUM_EVENTS 2

static struct {
	int fd;
	int overflows;
	int individual_overflow;
} events[NUM_EVENTS];

static struct {
	char name[BUFSIZ];
	int type;
	int config;
	int period;
} event_values[NUM_EVENTS] = {
	{
		.name   = "perf::instructions",
		.type   = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_INSTRUCTIONS,
		.period = 1000000,
	},
	{
		.name   = "perf::instructions",
		.type   = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_INSTRUCTIONS,
		.period = 2000000,
	},
};

static void our_handler(int signum __maybe_unused, siginfo_t *info,
			void *uc __maybe_unused)
{
	int i;
	int fd = info->si_fd;

	for (i = 0; i < NUM_EVENTS; i++) {
		if (events[i].fd == fd) {
			events[i].overflows++;
			break;
		}
	}

	if (i == NUM_EVENTS)
		printf("fd %d not found\n", fd);

	ioctl(fd, PERF_EVENT_IOC_REFRESH, 1);
}

int test__vw_simulate_oneshot_group_overflow(void)
{
	size_t mmap_size = (1 + MMAP_PAGES) * page_size;
	int ret, i, matches = 0;
	struct perf_event_attr pe;
	struct sigaction sa;
	void *our_mmap[NUM_EVENTS];
	double err;
	int result;
	long long values[1 + 1 * NUM_EVENTS], counts[NUM_EVENTS];

	for (i = 0; i < NUM_EVENTS; i++) {
		events[i].fd = -1;
		events[i].overflows = 0;
	}

	pr_debug("This tests simultaneous overflow within group in one-shot mode.\n");

	/* set up our signal handler */
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_sigaction = our_handler;
	sa.sa_flags = SA_SIGINFO;

	if (sigaction(SIGRTMIN + 2, &sa, NULL) < 0) {
		pr_err("Error setting up signal handler\n");
		return TEST_FAIL;
	}

	/* get expected counts */
	for (i = 0; i < NUM_EVENTS; i++) {
		memset(&pe, 0, sizeof(struct perf_event_attr));
		pe.type = event_values[i].type;
		pe.size = sizeof(struct perf_event_attr);
		pe.config = event_values[i].config;
		pe.sample_period = event_values[i].period;
		pe.sample_type = 0;
		pe.read_format = PERF_FORMAT_GROUP;
		pe.disabled = 1;
		pe.pinned = 0;
		pe.exclude_kernel = 1;
		pe.exclude_hv = 1;
		pe.wakeup_events = 1;

		events[i].fd = sys_perf_event_open(&pe, 0, -1, -1, 0);
		if (events[i].fd < 0) {
			pr_err("Error opening leader %llx\n", pe.config);
			return TEST_FAIL;
		}

		/* on older kernels you need this even if you don't use it */
		our_mmap[i] = mmap(NULL, mmap_size, PROT_READ|PROT_WRITE,
				   MAP_SHARED, events[i].fd, 0);

		fcntl(events[i].fd, F_SETFL, O_RDWR|O_NONBLOCK|O_ASYNC);
		fcntl(events[i].fd, F_SETSIG, SIGRTMIN + 2);
		fcntl(events[i].fd, F_SETOWN, getpid());

		ioctl(events[i].fd, PERF_EVENT_IOC_RESET, 0);

		pr_debug("Testing matrix matrix multiply\n");
		pr_debug("\tEvent %s with period %d\n",
			 event_values[i].name,
			 event_values[i].period);

		ret = ioctl(events[i].fd, PERF_EVENT_IOC_REFRESH, 1);

		pr_debug("\t");
		naive_matrix_multiply();

		ret = ioctl(events[i].fd, PERF_EVENT_IOC_DISABLE, 0);

		pr_debug("\tfd %d overflows: %d (%s/%d)\n",
			 events[i].fd, events[i].overflows,
			 event_values[i].name,
			 event_values[i].period);

		if (events[i].overflows == 0) {
			pr_err("No overflow events generated.\n");
			return TEST_FAIL;
		}

		munmap(our_mmap[i], mmap_size);
		close(events[i].fd);

		events[i].individual_overflow = events[i].overflows;
		events[i].overflows = 0;
		events[i].fd = -1;
	}

	/* test overflow for both */

	pr_debug("Testing matrix matrix multiply\n");
	for (i = 0; i < NUM_EVENTS; i++) {
		pr_debug("\tEvent %s with period %d\n",
			 event_values[i].name,
			 event_values[i].period);
	}

	for (i = 0; i < NUM_EVENTS; i++) {
		memset(&pe, 0, sizeof(struct perf_event_attr));
		pe.type = event_values[i].type;
		pe.size = sizeof(struct perf_event_attr);
		pe.config = event_values[i].config;
		pe.sample_period = event_values[i].period;
		pe.sample_type = 0;
		pe.read_format = PERF_FORMAT_GROUP;
		pe.disabled = (i == 0) ? 1 : 0;
		pe.pinned = (i == 0) ? 1 : 0;
		pe.exclude_kernel = 1;
		pe.exclude_hv = 1;
		pe.wakeup_events = 1;

		events[i].fd = sys_perf_event_open(&pe, 0, -1,
					(i == 0) ? -1 : events[0].fd, 0);
		if (events[i].fd < 0) {
			pr_err("Error opening leader %llx\n", pe.config);
			return TEST_FAIL;
		}

		/* on older kernels you need this even if you don't use it */
		our_mmap[i] = mmap(NULL, mmap_size, PROT_READ|PROT_WRITE,
				   MAP_SHARED, events[i].fd, 0);

		fcntl(events[i].fd, F_SETFL, O_RDWR|O_NONBLOCK|O_ASYNC);
		fcntl(events[i].fd, F_SETSIG, SIGRTMIN + 2);
		fcntl(events[i].fd, F_SETOWN, getpid());

		ioctl(events[i].fd, PERF_EVENT_IOC_RESET, 0);
	}

	ret = ioctl(events[0].fd, PERF_EVENT_IOC_REFRESH, 1);
	if (ret < 0) {
		pr_err("Error with PERF_EVENT_IOC_REFRESH of group leader: "
		       "%d %s\n", errno, strerror(errno));
		return TEST_FAIL;
	}

	pr_debug("\t");
	naive_matrix_multiply();

	ret = ioctl(events[0].fd, PERF_EVENT_IOC_DISABLE, 0);

	for (i = 0; i < NUM_EVENTS; i++) {
		pr_debug("\tfd %d overflows: %d (%s/%d)\n",
			 events[i].fd, events[i].overflows,
			 event_values[i].name, event_values[i].period);
	}

	result = read(events[0].fd, &values, 8 * (1 + 1 * NUM_EVENTS));

	if (result != 8 * (1 + 1 * NUM_EVENTS)) {
		pr_err("error reading\n");
		return TEST_FAIL;
	}

	if (values[0] != NUM_EVENTS) {
		pr_err("error reading\n");
		return TEST_FAIL;
	}

	for (i = 0; i < NUM_EVENTS; i++) {
		counts[i] = values[i + 1];
		pr_debug("\tCount %d: %lld\n", i, counts[i]);
	}

	for (i = 0; i < NUM_EVENTS; i++) {
		if (events[i].overflows == 0) {
			pr_err("No overflow events generated.\n");
			return TEST_FAIL;
		}
	}

	for (i = 0; i < NUM_EVENTS; i++) {
		munmap(our_mmap[i], mmap_size);
		close(events[i].fd);
	}

	/* test validity */
	for (i = 0; i < NUM_EVENTS; i++) {
		pr_debug("Event %s/%d Expected %lld Got %d "
			 "Individual overflow %d\n",
			 event_values[i].name, event_values[i].period,
			 counts[i]/event_values[i].period,
			 events[i].overflows,
			 events[i].individual_overflow);

		if (events[i].overflows != events[i].individual_overflow)
			return TEST_FAIL;

		if (counts[i]/event_values[i].period == events[i].overflows)
			matches++;
	}

	/*
	 * Counts will be slightly different because they will count
	 * while signal handler running.
	 */
	err  = (double) counts[0] - (double) counts[1];
	err /= (double) counts[0];
	err *= 100.0;

	if ((err > 1.0) ||  (err < -1.0)) {
		pr_err("Counts should be roughly the same "
		       "but found %lf%% error\n", err);
		return TEST_FAIL;
	}

	if (matches != NUM_EVENTS) {
		pr_err("Unexpected event count!\n");
		return TEST_FAIL;
	}

	return TEST_OK;
}
