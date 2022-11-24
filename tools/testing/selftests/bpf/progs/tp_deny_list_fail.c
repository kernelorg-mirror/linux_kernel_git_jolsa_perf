// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

#include "task_kfunc_common.h"

char _license[] SEC("license") = "GPL";

int do_printk = 0;

SEC("tp_btf/contention_begin")
int BPF_PROG(tp_btf_contention_begin_printk)
{
	// call bpf_trace_printk
	if (do_printk)
		bpf_printk("test\n");

	return 0;
}

SEC("tp_btf/bpf_trace_printk")
int BPF_PROG(tp_btf_bpf_trace_printk_printk)
{
	// call bpf_trace_vprintk
	if (do_printk)
		bpf_printk("test %d %d %d %d %d %d\n", 1, 2, 3, 4, 5, 6);

	return 0;
}
