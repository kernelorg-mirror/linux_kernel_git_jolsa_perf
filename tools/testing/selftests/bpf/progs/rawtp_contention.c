// SPDX-License-Identifier: GPL-2.0
#include <stdbool.h>
#include <stddef.h>
#include <linux/bpf.h>
#include <linux/ptrace.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

SEC("raw_tp/contention_begin")
int prog1(struct bpf_raw_tracepoint_args *ctx)
{
	bpf_printk("KRAVA1\n");
	return 0;
}

SEC("raw_tp/contention_begin")
int prog2(struct bpf_raw_tracepoint_args *ctx)
{
	bpf_printk("KRAVA2\n");
	return 0;
}

SEC("raw_tp/contention_begin")
int prog3(struct bpf_raw_tracepoint_args *ctx)
{
	bpf_printk("KRAVA3\n");
	return 0;
}

SEC("raw_tp/contention_begin")
int prog4(struct bpf_raw_tracepoint_args *ctx)
{
	bpf_printk("KRAVA4\n");
	return 0;
}

SEC("raw_tp/contention_begin")
int prog5(struct bpf_raw_tracepoint_args *ctx)
{
	bpf_printk("KRAVA5\n");
	return 0;
}

SEC("raw_tp/contention_begin")
int prog6(struct bpf_raw_tracepoint_args *ctx)
{
	bpf_printk("KRAVA6\n");
	return 0;
}

SEC("raw_tp/contention_begin")
int prog7(struct bpf_raw_tracepoint_args *ctx)
{
	bpf_printk("KRAVA7\n");
	return 0;
}

SEC("raw_tp/contention_begin")
int prog8(struct bpf_raw_tracepoint_args *ctx)
{
	bpf_printk("KRAVA8\n");
	return 0;
}

#if 0
SEC("raw_tp/bpf_trace_printk")
int prog1(struct bpf_raw_tracepoint_args *ctx)
{
	bpf_printk("DEBIL\n");
	return 0;
}
#endif

char _license[] SEC("license") = "GPL";
