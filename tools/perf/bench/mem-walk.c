
#include <math.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/bitmap.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <sys/prctl.h>

#include "parse-options.h"
#include "bench.h"
#include "debug.h"

static const char * const bench_mem_walk_usage[] = {
	"perf bench mem walk <options>",
	NULL
};

static unsigned int iterations = 1;
static unsigned int pad = 7;
static uint64_t size = 1 * 1024 * 1024;
static unsigned int psize;
static bool walk_random;

static const struct option options[] = {
	OPT_UINTEGER('i', "iterations", &iterations,
		     "repeat walk this number of times"),
	OPT_UINTEGER('p', "pad", &pad,
		     "pad size"),
	OPT_U64('s', "set-size", &size,
		     "working set size"),
	OPT_UINTEGER('S', "power-size", &psize,
		     "working set size by power of 2"),
	OPT_BOOLEAN('r', "random", &walk_random,
		    "random walk"),
	OPT_INCR('v', "verbose", &verbose,
		 "random walk"),
	OPT_END()
};

struct elem {
	struct elem *n;
	unsigned long pad[];
};

static unsigned int elem_size;

static struct elem *elems;
static unsigned int elems_cnt;

#define ELEM(idx)						\
({								\
	BUG_ON(idx >= elems_cnt);				\
	(struct elem *) ((char *) elems + (elem_size * idx));	\
})

static int init_elems_random(void)
{
	struct elem *e = ELEM(0);
	struct elem *first = e;
	unsigned long *bm;
	unsigned int i = 0;
	int ret = 0;

	bm = malloc(sizeof(bm) * BITS_TO_LONGS(elems_cnt));
	if (!bm)
		return -ENOMEM;

	if (elems_cnt > RAND_MAX) {
		pr_err("failed: elems number higher than random"
		       " generator max.\n");
		ret = -EINVAL;
		goto out;
	}

	srand(time(NULL));
	bitmap_fill(bm, elems_cnt);

	/* first elem is fixed */
	clear_bit(0, bm);

	pr_debug4("%010d %p\n", 0, e);

	while (++i < elems_cnt) {
		struct elem *ne;

		unsigned int idx = rand() % elems_cnt;

		if (!test_bit(idx, bm)) {
			idx = find_next_bit(bm, elems_cnt, idx);
			if (idx == elems_cnt) {
				idx = find_next_bit(bm, elems_cnt, 0);
				BUG_ON(idx == elems_cnt);
			}
		}

		clear_bit(idx, bm);
		ne = ELEM(idx);

		e->n = ne;
		e = ne;

		pr_debug4("%010d %p\n", i, e);
	}

	e->n = first;

out:
	free(bm);
	return ret;
}

static int init_elems_seq(void)
{
	struct elem *e = ELEM(0);
	struct elem *first = e;
	unsigned int i = 0;

	pr_debug4("%010d %p\n", 0, e);

	while (++i < elems_cnt) {
		struct elem *ne = ELEM(i);

		e->n = ne;
		e = ne;

		pr_debug4("%010d %p\n", i, e);
	}

	e->n = first;
	return 0;
}

static int init_elems(bool random_walk)
{
	pr_debug("running init\n");
	return random_walk ? init_elems_random() : init_elems_seq();
}

static void walk_elems(void)
{
	struct elem *first = ELEM(0);
	struct elem *e = first;

	pr_debug("running walk\n");

	do {
		pr_debug4("%p\n", e);
		e = e->n;
	} while (e != first);
}

static void walk(void)
{
	unsigned int i;

	prctl(PR_TASK_PERF_EVENTS_ENABLE, 0, 0, 0, 0);

	for (i = 0; i < iterations; i++)
		walk_elems();

	prctl(PR_TASK_PERF_EVENTS_DISABLE, 0, 0, 0, 0);
}

int bench_mem_walk(int argc, const char **argv,
		   const char *prefix __maybe_unused)
{
	argc = parse_options(argc, argv, options,
			     bench_mem_walk_usage, 0);

	if (psize)
		size = powl(2, psize);

	elem_size = sizeof(struct elem *) +
		    sizeof(unsigned long) * pad;

	elems_cnt = size / elem_size;

	elems = zalloc(size);
	if (!elems) {
		pr_err("failed to allocate working set\n");
		return -1;
	}

	if (mlockall(MCL_CURRENT)) {
		perror("failed to lock memory");
		goto out;
	}

	pr_info("working set size : %" PRIu64 "\n", size);
	pr_info("element size     : %u\n", elem_size);
	pr_info("elements count   : %u\n", elems_cnt);
	pr_info("iteration count  : %u\n", iterations);
	pr_info("walk             : %s\n",
		walk_random ? "random" : "sequential");

	if (init_elems(walk_random))
		goto out_unlock;

	walk();

 out_unlock:
	munlockall();

 out:
	free(elems);
	return 0;
}
