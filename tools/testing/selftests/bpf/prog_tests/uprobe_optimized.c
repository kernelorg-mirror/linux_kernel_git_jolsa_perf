// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "uprobe_optimized.skel.h"

#define MAP "[uprobes]"

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

static void dump_stack(const char *msg, void *start)
{
	unsigned long *p;
	int i;

	fprintf(stderr, "---> stack %s\n", msg);
	p = (unsigned long *) start;
	for (i = 0; i < 20; i++) {
		fprintf(stderr, "%lx: %012lx\n", (unsigned long) &p[i], p[i]);
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
		if (2 != sscanf(line, "%p-%p --xp %*x %*x:%*x %*u %n",
				&start, &end, &m))
			continue;
		if (m < 0)
			continue;

		if (!strncmp(&line[m], MAP, sizeof(MAP)-1)) {
			fprintf(stderr, "map found %lx-%lx\n", (unsigned long) start, (unsigned long) end);
			dump("[uprobes]", start, 100);
			break;
		}
	}

	fclose(maps);
	return 0;
}

noinline int uprobe_test(void)
{
	unsigned long ptr;

	dump_stack("uprobe_test", &ptr);
	find_uprobe_map();
	return 1;
}

void test_uprobe_optimized(void)
{
	LIBBPF_OPTS(bpf_uprobe_multi_opts, opts,
		.retprobe = true,
	);
	struct uprobe_optimized *skel;
	int err;

	skel = uprobe_optimized__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_optimized__open_and_load"))
		return;

	skel->links.test = bpf_program__attach_uprobe_multi(skel->progs.test, 0,
							    "/proc/self/exe",
							    "uprobe_test",
							    &opts);
	if (!ASSERT_OK_PTR(skel->links.test, "bpf_program__attach_uprobe_multi"))
		goto cleanup;

	dump("uprobe_test", uprobe_test, 50);

	fprintf(stderr, "/proc/%d/maps\n", getpid());

	err = uprobe_test();
	getchar();

	fprintf(stderr, "KRAVA err %d\n", err);

cleanup:
	uprobe_optimized__destroy(skel);
}
