
#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <sched.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <stdlib.h>

#define SMP_CACHE_BYTES 64
#define cacheline_aligned __attribute__((__aligned__(SMP_CACHE_BYTES)))

struct krava {
	unsigned long a;
	unsigned long b;
#if 0
	unsigned long a cacheline_aligned;
	unsigned long b cacheline_aligned;
#endif
};

static struct krava k;
#define MAX (unsigned long) 3000000000

static void set_cpu(unsigned long cpu)
{
	cpu_set_t set;
	pid_t tid = syscall(SYS_gettid);

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);

	if (sched_setaffinity(tid, sizeof(set), &set) == -1)
		fprintf(stderr, "failed to set cpu\n");

	fprintf(stderr, "tid %d\n", tid);
}

void *worker1(void *arg)
{
	unsigned long i;
	unsigned long tmp;

	set_cpu((unsigned long) arg);

	for (i = 0; i < MAX; i++) {
#if 0
		if (i % 10000000 == 0)
			fprintf(stderr, ".");
#endif
		tmp += k.a;
	}

	return NULL;
}

void *worker2(void *arg)
{
	unsigned long i;

	set_cpu((unsigned long) arg);

	for (i = 0; i < MAX; i++) {
#if 0
		if (i % 10000000 == 0)
			fprintf(stderr, ".");
#endif
		k.b = i;
	}

	return NULL;
}

int main(int argc, char **argv)
{
	pthread_t thread1;
	pthread_t thread2;
	unsigned long cpu1, cpu2;

	if (argc != 3) {
		fprintf(stderr, "%s cpu1 cpu2\n", argv[0]);
		return -1;
	}

	cpu1 = atol(argv[1]);
	cpu2 = atol(argv[2]);

	pthread_create(&thread1, NULL, worker1, (void *) cpu1);
	pthread_create(&thread2, NULL, worker2, (void *) cpu2);

	pthread_join(thread1, NULL);
	pthread_join(thread2, NULL);
	return 0;
}
