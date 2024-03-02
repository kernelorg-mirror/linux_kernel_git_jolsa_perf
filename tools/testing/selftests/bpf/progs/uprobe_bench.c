// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <stdbool.h>

char _license[] SEC("license") = "GPL";

__u64 uprobe1_cnt = 0;
__u64 uprobe2_cnt = 0;

SEC("uprobe//proc/self/exe:uprobe1_bench_trigger")
int uprobe1(struct pt_regs *ctx)
{
	uprobe1_cnt++;
	return 0;
}

SEC("uprobe//proc/self/exe:uprobe2_bench_trigger+1")
int uprobe2(struct pt_regs *ctx)
{
	uprobe2_cnt++;
	return 0;
}
