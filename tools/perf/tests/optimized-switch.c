#include <unistd.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <linux/compiler.h>
#include "tests.h"
#include "debug.h"
#include "evsel.h"
#include "evlist.h"
#include "thread_map.h"

#define GROUPS	10
#define WORKERS	10
#define BYTES	100

struct worker {
	union {
		int pipefd[2];
		struct {
			int read;
			int write;
		} fd;
	};
};

#define pr_errsys(s) \
	pr_err(s " failed: %s\n", strerror(errno))

static void worker(int fd)
{
	unsigned char c = 0;
	int cnt = BYTES;
	ssize_t r;

	while (cnt--) {
		r = read(fd, &c, sizeof(c));
		if (r != 1) {
			pr_errsys("read");
			exit(-1);
		}
	}

	prctl(0, 0, 0, 0, 0);
	exit(0);
}

static void write_round(struct worker *workers)
{
	unsigned char c = 0;
	ssize_t r;
	int i, j;

	for (j = 0; j < BYTES; j++) {
		for (i = 0; i < WORKERS; i++) {
			struct worker *w = &workers[i];

			r = write(w->fd.write, &c, sizeof(c));
			if (r != 1) {
				pr_errsys("write");
				exit(-1);
			}
		}
	}
}

static int group(void)
{
	struct worker workers[WORKERS];
	int i;

	for (i = 0; i < WORKERS; i++) {
		struct worker *w = &workers[i];
		int pid;

		if (pipe(w->pipefd)) {
			pr_errsys("pipe");
			return -1;
		}

		pid = fork();
		if (pid == -1) {
			pr_errsys("fork");
			return -1;
		} else if (!pid) {
			worker(w->fd.read);
		}
	}

	write_round(workers);

	for (i = 0; i < WORKERS; i++)
		wait(NULL);

	return 0;
}

static int child(void)
{
	int i;

	for (i = 0; i < GROUPS; i++)
		TEST_ASSERT_VAL("group failed", !group());

	return 0;
}

int test__optimized_switch(void)
{
	ssize_t r __maybe_unused;
	unsigned char c = 0;
	int ready[2], pid;
	struct thread_map *threads;
	struct perf_evsel *evsel;
	u64 total;

	if (pipe(ready)) {
		pr_errsys("pipe");
		return -1;
	}

	pid = fork();
	if (pid == -1) {
		pr_errsys("fork");
		return -1;
	}

	if (!pid) {
		r = read(ready[0], &c, sizeof(c));
		if (r != 1) {
			pr_errsys("read");
			exit(-1);
		}

		exit(child());
	}

	threads = thread_map__new(-1, pid, UINT_MAX);

	evsel = perf_evsel__newtp("syscalls", "sys_enter_prctl");
	if (evsel == NULL) {
		pr_debug("perf_evsel__open_per_thread failed\n");
		return -1;
	}

	evsel->attr.inherit = 1;

	if (perf_evsel__open_per_thread(evsel, threads) < 0) {
		pr_err("perf_evsel__newtp failed\n");
		return -1;
	}

	r = write(ready[1], &c, sizeof(c));
	if (r != 1) {
		pr_errsys("write");
		return -1;
	}

	wait(NULL);

	if (perf_evsel__read_on_cpu(evsel, 0, 0) < 0) {
		pr_err("perf_evsel__read_on_cpu failed\n");
		return -1;
	}

	total = evsel->counts->cpu[0].val;

	pr_debug("total count %lu, expected count %d\n", total, GROUPS * WORKERS);
	TEST_ASSERT_VAL("wrong count", total == GROUPS * WORKERS);
	return 0;
}
