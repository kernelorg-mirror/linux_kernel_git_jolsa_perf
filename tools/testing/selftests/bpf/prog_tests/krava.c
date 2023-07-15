// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <test_progs.h>
#include "krava.skel.h"
#include "bpf/libbpf_internal.h"

static int duration;

__attribute__((no_sanitize_address))
static void on_sample(void *ctx, int cpu, void *data, __u32 size)
{
	printf("SAMPLE cpu %d\n", cpu);
}

void test_krava(void)
{
	struct perf_buffer *pb;
	struct krava *skel;
	int err;

	skel = krava__open_and_load();
	if (CHECK(!skel, "skel_load", "skeleton open/load failed\n"))
		goto out_close;

	err = krava__attach(skel);
	if (CHECK(err, "attach_kprobe", "err %d\n", err))
		goto out_close;

	pb = perf_buffer__new(bpf_map__fd(skel->maps.perfbuf), 1,
			      on_sample, NULL, NULL, NULL);
	if (!ASSERT_OK_PTR(pb, "perf_buf__new"))
		goto out_close;

	while (1) {
		err = perf_buffer__poll(pb, 100);
		if (CHECK(err < 0, "perf_buffer__poll", "err %d\n", err))
			goto out_free_pb;
	}

out_free_pb:
	perf_buffer__free(pb);
out_close:
	krava__destroy(skel);
}
