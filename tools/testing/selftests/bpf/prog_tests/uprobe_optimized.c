// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <unistd.h>
#include <test_progs.h>
#include "uprobe_optimized.skel.h"
#include "uprobe_optimized_usdt.skel.h"
#include "../sdt.h"

__naked noinline void uprobe_test(void)
{
	asm volatile (".byte 0x0f\n\t"
		      ".byte 0x1f\n\t"
		      ".byte 0x44\n\t"
		      ".byte 0x00\n\t"
		      ".byte 0x00\n\t"
		      "ret\n\t");
}

#define MAP "[uprobes-trampoline]"

static void dump(const char *msg, void *start, int size)
{
	unsigned char *p;
	int i;

	fprintf(stderr, "---> %s\n", msg);
	fprintf(stderr, "Code: ");
	p = (unsigned char *) start;
	for (i = 0; i < 100; i++) {
		fprintf(stderr, "%02x ", (unsigned char) p[i]);
	}
	fprintf(stderr, "\n");
}

static int find_uprobe_map(void)
{
	void *start = NULL, *end = NULL;
	char line[128];
	FILE *maps;

	maps = fopen("/proc/self/maps", "r");
	if (!maps) {
		fprintf(stderr, "cannot open maps\n");
		return -1;
	}

	while (fgets(line, sizeof(line), maps)) {
		int m = -1;

		/* We care only about private r-x mappings. */
		if (2 != sscanf(line, "%p-%p r-xp %*x %*x:%*x %*u %n",
				&start, &end, &m))
			continue;
		if (m < 0)
			continue;

		if (!strncmp(&line[m], MAP, sizeof(MAP)-1)) {
			fprintf(stderr, "map found %lx-%lx\n", (unsigned long) start, (unsigned long) end);
			dump("optimized-uprobes", start, 100);
			break;
		}
	}

	fclose(maps);
	return 0;
}

static void test_debug(void)
{
	LIBBPF_OPTS(bpf_uprobe_multi_opts, opts);
	struct uprobe_optimized *skel;

	skel = uprobe_optimized__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_optimized__open_and_load"))
		return;

	dump("BEFORE uprobe_test", uprobe_test, 50);

	skel->links.test = bpf_program__attach_uprobe_multi(skel->progs.test, 0,
							    "/proc/self/exe",
							    "uprobe_test",
							    NULL);
	if (!ASSERT_OK_PTR(skel->links.test, "bpf_program__attach_uprobe_multi"))
		goto cleanup;

	find_uprobe_map();
	dump("ACTIVE uprobe_test", uprobe_test, 50);

	fprintf(stderr, "/proc/%d/maps\n", getpid());

	getchar();
	uprobe_test();

	ASSERT_EQ(skel->bss->executed, 1, "executed");

	bpf_link__destroy(skel->links.test);
	skel->links.test = NULL;

	dump("AFTER  uprobe_test", uprobe_test, 50);

cleanup:
	uprobe_optimized__destroy(skel);
}

static void *worker(void*)
{
	fprintf(stderr, "WORKER %d\n", gettid());

	while (1) {
		uprobe_test();
	}
	return NULL;
}

static void test_race(void)
{
	struct uprobe_optimized *skel;
	pthread_t threads[1];
	unsigned int i;
	int err;

	fprintf(stderr, "RACE\n");

	for (i = 0; i < ARRAY_SIZE(threads); i++) {
		fprintf(stderr, "THREAD %i - %d\n", i, gettid());
		err = pthread_create(&threads[i], NULL, worker, NULL);
		if (!ASSERT_OK(err, "new toggler"))
			return;
	}

	skel = uprobe_optimized__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_optimized__open_and_load"))
		return;

	i = 0;
	while (1) {
		skel->links.test = bpf_program__attach_uprobe_multi(skel->progs.test, 0,
								    "/proc/self/exe",
								    "uprobe_test",
								    NULL);
		if (!ASSERT_OK_PTR(skel->links.test, "bpf_program__attach_uprobe_multi"))
			goto cleanup;

		fprintf(stderr, "ROUND %i - hits %d\n", i, skel->bss->executed);

		//bpf_link__destroy(skel->links.test);
		//skel->links.test = NULL;
		i++;
	}

cleanup:
	uprobe_optimized__destroy(skel);
}

noinline void usdt_test(void)
{
	STAP_PROBE(trigger, usdt);
}

static void test_usdt_debug(void)
{
	struct uprobe_optimized_usdt *skel;

	skel = uprobe_optimized_usdt__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_optimized__open_and_load"))
		return;

	skel->links.usdt0 = bpf_program__attach_usdt(skel->progs.usdt0,
						    -1 /* all PIDs */, "/proc/self/exe",
						    "trigger", "usdt", NULL);
	if (!skel->links.usdt0)
		goto cleanup;

	find_uprobe_map();
	dump("ACTIVE uprobe_test", uprobe_test, 50);
	fprintf(stderr, "/proc/%d/maps\n", getpid());
	getchar();

	usdt_test();

cleanup:
	uprobe_optimized_usdt__destroy(skel);
}

void test_uprobe_optimized(void)
{
	if (test__start_subtest("debug"))
		test_debug();
	if (test__start_subtest("race"))
		test_race();
	if (test__start_subtest("usdt"))
		test_usdt_debug();
}
