// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include <linux/bpf.h>
#include <stdint.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(value_size, sizeof(int));
	__uint(key_size, sizeof(int));
} perfbuf SEC(".maps");

const volatile int batch_cnt = 10;

long sample_val = 42;
long dropped __attribute__((aligned(128))) = 0;

static __always_inline void output(void *ctx)
{
	int i;

	for (i = 0; i < batch_cnt; i++) {
		if (bpf_perf_event_output(ctx, &perfbuf, BPF_F_CURRENT_CPU,
					  &sample_val, sizeof(sample_val)))
			__sync_add_and_fetch(&dropped, 1);
	}
}

SEC("uprobe/./test:krava")
int krava1(void *ctx)
{
	output(ctx);
	return 0;
}

SEC("kprobe/ksys_read")
int krava2(void *ctx)
{
	output(ctx);
	return 0;
}
