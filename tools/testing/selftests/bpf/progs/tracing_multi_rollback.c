// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

SEC("fentry.multi")
int BPF_PROG(fentry_multi)
{
	return 0;
}

SEC("fexit.multi")
int BPF_PROG(fexit_multi)
{
	return 0;
}

SEC("fentry/bpf_fentry_test3")
int BPF_PROG(prog3, char a, int b, __u64 c)
{
	return 0;
}

SEC("fexit/bpf_fentry_test4")
int BPF_PROG(prog4, void *a, char b, int c, __u64 d)
{
	return 0;
}

SEC("fentry/bpf_fentry_test5")
int BPF_PROG(prog5, __u64 a, void *b, short c, int d, __u64 e)
{
	return 0;
}
