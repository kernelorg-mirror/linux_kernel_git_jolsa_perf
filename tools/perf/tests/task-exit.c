#include "evlist.h"
#include "evsel.h"
#include "thread_map.h"
#include "cpumap.h"
#include "tests.h"

#include <signal.h>

static int exited;
static int nr_exit;

static void sig_handler(int sig)
{
	exited = 1;

	if (sig == SIGUSR1)
		nr_exit = -1;
}

/*
 * This test will start a workload (for givem cpu and thread maps)
 * that does nothing then it checks if the number of exit event
 * reported by the kernel is 1 or not in order to check the kernel
 * returns correct number of event.
 */
static int run_test(struct cpu_map *cpus, struct thread_map *threads,
		    bool match_pid, bool system_wide)
{
	int i, err = -1;
	struct perf_evsel *evsel;
	struct perf_evlist *evlist;
	struct perf_target target = {
		.uid		= UINT_MAX,
		.uses_mmap	= true,
		.system_wide	= system_wide,
	};
	const char *argv[] = { "true", NULL };

	signal(SIGCHLD, sig_handler);
	signal(SIGUSR1, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGINT, sig_handler);

	evlist = perf_evlist__new();
	if (evlist == NULL) {
		pr_debug("perf_evlist__new\n");
		return -1;
	}

	perf_evlist__set_maps(evlist, cpus, threads);

	/*
	 * We need at least one evsel in the evlist, use the default
	 * one: "cycles".
	 */
	err = perf_evlist__add_default(evlist);
	if (err < 0) {
		pr_debug("Not enough memory to create evsel\n");
		goto out_free_evlist;
	}

	err = perf_evlist__prepare_workload(evlist, &target, argv, false, true);
	if (err < 0) {
		pr_debug("Couldn't run the workload!\n");
		goto out_delete_maps;
	}

	evsel = perf_evlist__first(evlist);
	evsel->attr.task = 1;
	evsel->attr.sample_freq = 0;
	evsel->attr.inherit = 0;
	evsel->attr.watermark = 0;
	evsel->attr.wakeup_events = 1;
	evsel->attr.exclude_kernel = 1;

	err = perf_evlist__open(evlist);
	if (err < 0) {
		pr_debug("Couldn't open the evlist: %s\n", strerror(-err));
		goto out_delete_maps;
	}

	if (perf_evlist__mmap(evlist, 128, true) < 0) {
		pr_debug("failed to mmap events: %d (%s)\n", errno,
			 strerror(errno));
		goto out_close_evlist;
	}

	perf_evlist__start_workload(evlist);

	pr_debug("workload pid %d\n", evlist->workload.pid);

retry:
	pr_debug("received %d EXIT records\n", nr_exit);

	for (i = 0; i < evlist->nr_mmaps; i++) {
		struct comm_event *comm;
		union perf_event *event;

		event = perf_evlist__mmap_read(evlist, i);
		if (!event)
			continue;

		if (event->header.type != PERF_RECORD_EXIT)
			continue;

		comm = (struct comm_event *) event;

		pr_debug("received EXIT event, pid %d, map idx %d\n",
			  comm->pid, i);

		if (!match_pid || ((pid_t) comm->pid == evlist->workload.pid))
			nr_exit++;
	}

	if (!exited || !nr_exit) {
		poll(evlist->pollfd, evlist->nr_fds, 100);
		goto retry;
	}

	pr_debug("received %d EXIT records\n", nr_exit);

	if (nr_exit != 1)
		err = -1;

	perf_evlist__munmap(evlist);
out_close_evlist:
	perf_evlist__close(evlist);
out_delete_maps:
	perf_evlist__delete_maps(evlist);
out_free_evlist:
	perf_evlist__delete(evlist);
	return err;
}

/*
 * Testing pid based event to get proper number of EXIT
 * auxiliary events.
 */
int test__task_exit_task(void)
{
	/*
	 * Create maps of threads and cpus to monitor. In this case
	 * we start with all threads and cpus (-1, -1) but then in
	 * perf_evlist__prepare_workload we'll fill in the only thread
	 * we're monitoring, the one forked there.
	 */
	struct cpu_map *cpus = cpu_map__dummy_new();
	struct thread_map *threads = thread_map__new_by_tid(-1);

	if (!cpus || !threads) {
		pr_debug("Not enough memory to create thread/cpu maps\n");
		return TEST_FAIL;
	}

	return run_test(cpus, threads, false, false) ? TEST_FAIL : TEST_OK;
}

/*
 * Testing cpu based event to get proper number of EXIT
 * auxiliary events.
 */
int test__task_exit_cpu(void)
{
	/*
	 * Create maps of cpus to monitor. In this case we create
	 * cpu map for all cpus and leave threads maps as NULL.
	 * The threads map will be dummied by perf_evsel__open.
	 */
	struct cpu_map *cpus = cpu_map__new(NULL);

	if (!cpus) {
		pr_debug("Not enough memory to create cpu map\n");
		return TEST_FAIL;
	}

	return run_test(cpus, NULL, true, true) ? TEST_FAIL : TEST_OK;
}
