/*
 * locking.c
 *
 * Simple micro benchmark that stress kernel locking contention with
 * creat(2) system call by spawning multiple processes to call
 * this system call.
 *
 * Results output are average operations/sec for all threads and
 * average operations/sec per threads.
 *
 * Tuan Bui <tuan.d.bui@hp.com>
 */

#include "../perf.h"
#include "../util/util.h"
#include "../util/stat.h"
#include "../util/parse-options.h"
#include "../util/header.h"
#include "bench.h"

#include <err.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/resource.h>
#include <linux/futex.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <signal.h>
#include <dirent.h>

struct worker {
	pid_t pid;
	unsigned int order_id;
	char str[50];
};

struct timeval start, end, total;
static unsigned int start_nr_threads = 100;
static unsigned int end_nr_threads = 1100;
static unsigned int increment_threads_by = 100;
static unsigned int bench_dur = 5;

/* Shared variables between fork processes*/
unsigned int *finished, *setup;
unsigned long long *shared_workers;
/* all processes will block on the same futex */
u_int32_t *futex;

static const struct option options[] = {
	OPT_UINTEGER('s', "start", &start_nr_threads, "Numbers of processes to start"),
	OPT_UINTEGER('e', "end", &end_nr_threads, "Numbers of process to end"),
	OPT_UINTEGER('i', "increment", &increment_threads_by, "Number of threads to increment)"),
	OPT_UINTEGER('r', "runtime", &bench_dur, "Specify benchmark runtime in seconds"),
	OPT_END()
};

static const char * const bench_locking_creat_usage[] = {
	"perf bench locking creat <options>",
	NULL
};

/* Running bench creat workload */
static void *run_bench_creat(struct worker *workers)
{
	int fd;
	unsigned long long nr_ops = 0;
	int ret;

	sprintf(workers->str, "%d-XXXXXX", getpid());
	ret = mkstemp(workers->str);
	if (ret < 0)
		err(EXIT_FAILURE, "mkstemp");

	/* Signal to parent process and wait till all threads are ready run */
	setup[workers->order_id] = 1;
	syscall(SYS_futex, futex, FUTEX_WAIT, 0, NULL, NULL, 0);

	/* Start of the benchmark keep looping till parent process signal completion */
	while (!*finished) {
		fd = creat(workers->str, S_IRWXU);
		if (fd < 0)
			err(EXIT_FAILURE, "creat");
		nr_ops++;
		close(fd);
	}

	unlink(workers->str);
	shared_workers[workers->order_id] = nr_ops;
	setup[workers->order_id] = 0;
	exit(0);
}

/* Setting shared variable finished and shared_workers */
static void setup_shared(void)
{
	unsigned int *finished_tmp, *setup_tmp;
	unsigned long long *shared_workers_tmp;
	u_int32_t *futex_tmp;

	/* finished shared var is use to signal start and end of benchmark */
	finished_tmp = (void *)mmap(0, sizeof(unsigned int), PROT_READ|PROT_WRITE,
			MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	if (finished_tmp == (void *) -1)
		err(EXIT_FAILURE, "mmap finished");
	finished = finished_tmp;

	/* shared_workers is an array of ops perform by each process */
	shared_workers_tmp = (void *)mmap(0, sizeof(unsigned long long)*end_nr_threads,
			PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	if (shared_workers_tmp == (void *) -1)
		err(EXIT_FAILURE, "mmap shared_workers");
	shared_workers = shared_workers_tmp;

	/* setup is use for each processes to signal that it is done
	 * setting up for the benchmark and is ready to run */
	setup_tmp = (void *)mmap(0, sizeof(unsigned int)*end_nr_threads,
			PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	if (setup_tmp == (void *) -1)
		err(EXIT_FAILURE, "mmap shared_workers");
	setup = setup_tmp;

	/* Processes will sleep on this futex until all other processes
	 * are done setting up and are ready to run */
	futex_tmp = (void *)mmap(0, sizeof(u_int32_t *), PROT_READ|PROT_WRITE,
			MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	if (futex_tmp == (void *) -1)
		err(EXIT_FAILURE, "mmap finished");
	futex = futex_tmp;
	(*futex) = 0;
}

/* Freeing shared variables */
static void free_resources(void)
{
	if ((munmap(finished, sizeof(unsigned int)) == -1))
		err(EXIT_FAILURE, "munmap finished");

	if ((munmap(shared_workers, sizeof(unsigned long long) * end_nr_threads) == -1))
		err(EXIT_FAILURE, "munmap shared_workers");

	if ((munmap(setup, sizeof(unsigned int) * end_nr_threads) == -1))
		err(EXIT_FAILURE, "munmap shared_workers");

	if ((munmap(futex, sizeof(u_int32_t))) == -1)
		err(EXIT_FAILURE, "munmap finished");
}

/* Start to spawn workers and wait till all workers have been
 * created before starting workload */
static void spawn_workers(void *(*bench_ptr) (struct worker *))
{
	pid_t parent, child;
	unsigned int i, j, k;
	struct worker workers;
	unsigned long long total_ops;
	unsigned int total_workers;

	parent = getpid();
	setup_shared();

	/* This loop through all the run each is increment by increment_threads_by */
	for (i = start_nr_threads; i <= end_nr_threads; i += increment_threads_by) {

		for (j = 0; j < i; j++) {
			if (!fork())
				break;
		}

		child = getpid();
		/* Initialize child worker struct and run benchmark */
		if (child != parent) {
			workers.order_id = j;
			workers.pid = child;
			bench_ptr(&workers);
		}
		/* Parent to sleep during the duration of benchmark */
		else{
			/* Make sure all child process are created and setup
			 * before starting benchmark for bench_dur durations */
			do {
				total_workers = 0;
				for (k = 0; k < i; k++)
					total_workers = total_workers + setup[k];
			} while (total_workers != i);

			/* Wake up all sleeping process to run the benchmark */
			(*futex) = 1;
			syscall(SYS_futex, futex, FUTEX_WAKE, i, NULL, NULL, 0);

			/* All proccesses are ready signal them to run */
			gettimeofday(&start, NULL);
			sleep(bench_dur);
			(*finished) = 1;
			gettimeofday(&end, NULL);
			timersub(&end, &start, &total);

			/* Wait for all process to terminate before getting outputs */
			for (k = 0; k < i; k++)
				wait(NULL);

			/* Sum up all the ops by each process and report */
			total_ops = 0;
			for (k = 0; k < i; k++)
				total_ops = total_ops + shared_workers[k];

			printf("\n%6d threads: throughput = %llu average opts/sec all threads\n",
				i, (total_ops / total.tv_sec));

			printf("%6d threads: throughput = %llu average opts/sec per thread\n",
				i, ((total_ops/total.tv_sec)/(!i ? 1:i)));

			/* Reset back to 0 for next run */
			(*finished) = 0;
			(*futex) = 1;
		}
	}
}

int bench_locking_creat(int argc, const char **argv,
			const char *prefix __maybe_unused)
{
	argc = parse_options(argc, argv, options, bench_locking_creat_usage, 0);

	if (argc) {
		usage_with_options(bench_locking_creat_usage, options);
		exit(EXIT_FAILURE);
	}

	spawn_workers(run_bench_creat);
	free_resources();
	return 0;
}
