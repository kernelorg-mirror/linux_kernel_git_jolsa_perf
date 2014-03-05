#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include "debug.h"
#include "tests.h"
#include "machine.h"
#include "thread_map.h"
#include "symbol.h"
#include "thread.h"

#define THREADS 4

static int go_away;

struct thread_data {
	pthread_t	pt;
	pid_t		tid;
	void		*map;
	int		ready[2];
};

static struct thread_data threads[THREADS];

static int thread_init(struct thread_data *td)
{
	void *map;

	map = mmap(NULL, page_size, PROT_READ|PROT_WRITE,
		   MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	if (map == MAP_FAILED) {
		perror("mmap failed");
		return -1;
	}

	td->map = map;
	td->tid = syscall(SYS_gettid);

	pr_debug("tid = %d, map = %p\n", td->tid, map);
	return 0;
}

static void* thread(void *arg)
{
	struct thread_data *td = arg;
	ssize_t ret;
	int go;

	if (thread_init(td))
		return NULL;

	ret = write(td->ready[1], &go, sizeof(int));
	if (ret != sizeof(int)) {
		pr_err("failed to notify\n");
		return NULL;
	}

	while (!go_away) {
		usleep(100);
	}

	munmap(td->map, page_size);
	return NULL;
}

static int thread_create(int i)
{
	struct thread_data *td = &threads[i];
	int err, go;

	if (pipe(td->ready))
		return -1;

	err = pthread_create(&td->pt, NULL, thread, td);
	if (!err) {
		ssize_t ret = read(td->ready[0], &go, sizeof(int));
		err = ret != sizeof(int);
	}

	return err;
}

static int threads_create(void)
{
	struct thread_data *td0 = &threads[0];
	int i, err = 0;

	if (thread_init(td0))
		return -1;

	for (i = 1; !err && i < THREADS; i++)
		err = thread_create(i);

	return err;
}

static int threads_destroy(void)
{
	struct thread_data *td0 = &threads[0];
	int i, err = 0;

	munmap(td0->map, page_size);

	go_away = 1;

	for (i = 1; !err && i < THREADS; i++)
		err = pthread_join(threads[i].pt, NULL);

	return err;
}

typedef int (*synth_cb)(struct machine *machine);

static int synth_all(struct machine *machine)
{
	return perf_event__synthesize_threads(NULL,
					      perf_event__process,
					      machine, 0);
}

static int synth_process(struct machine *machine)
{
	struct thread_map *map;
	int err;

	map = thread_map__new_by_tid(syscall(SYS_gettid));

	err = perf_event__synthesize_thread_map(NULL, map,
						perf_event__process,
						machine, 0);

	thread_map__delete(map);
	return err;
}

static int mmap_events(synth_cb synth)
{
	struct machines machines;
	struct machine *machine;
	int err, i;

	TEST_ASSERT_VAL("failed to create threads", !threads_create());

	machines__init(&machines);
	machine = &machines.host;

	dump_trace = verbose > 1 ? 1 : 0;

	err = synth(machine);

	dump_trace = 0;

	TEST_ASSERT_VAL("failed to destroy threads", !threads_destroy());
	TEST_ASSERT_VAL("failed to synthesize maps", !err);

	for (i = 0; i < THREADS; i++) {
		struct thread_data *td = &threads[i];
		struct addr_location al;
		struct thread *thread;

		thread = machine__findnew_thread(machine, getpid(), td->tid);

		pr_debug("looking for map %p\n", td->map);

		thread__find_addr_map(thread, machine,
				      PERF_RECORD_MISC_USER, MAP__FUNCTION,
				      (u64) (td->map + 1), &al);

		if (!al.map) {
			pr_debug("failed, couldn't find map\n");
			err = -1;
			break;
		}

		pr_debug("map %p, addr %lx\n", al.map, al.map->start);
	}

	machine__delete_threads(machine);
	machines__exit(&machines);
	return err;
}

int test__mmap_events(void)
{
	TEST_ASSERT_VAL("failed with sythesizing all",
			!mmap_events(synth_all));
	TEST_ASSERT_VAL("failed with sythesizing process",
			!mmap_events(synth_process));
	return 0;
}
