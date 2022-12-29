// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

__u64 test_result = 0;
__u64 test_ret_result = 0;

__hidden extern void multi_arg_check(__u64 *ctx, __u64 *test_result);
__hidden extern void multi_ret_check(void *ctx, __u64 *test_result);

SEC("fentry.multi")
int BPF_PROG(fentry_multi)
{
	multi_arg_check(ctx, &test_result);
	return 0;
}

SEC("fexit.multi")
int BPF_PROG(fexit_multi)
{
	multi_arg_check(ctx, &test_result);
	multi_ret_check(ctx, &test_ret_result);
	return 0;
}

__u64 test3_result = 0;
SEC("fentry/bpf_fentry_test3")
int BPF_PROG(prog3, char a, int b, __u64 c)
{
	test3_result = a == 4 && b == 5 && c == 6;
	return 0;
}

__u64 test4_result = 0;
SEC("fexit/bpf_fentry_test4")
int BPF_PROG(prog4, void *a, char b, int c, __u64 d)
{
	test4_result = a == (void *)7 && b == 8 && c == 9 && d == 10;
	return 0;
}

__u64 test5_result = 0;
SEC("fentry/bpf_fentry_test5")
int BPF_PROG(prog5, __u64 a, void *b, short c, int d, __u64 e)
{
	test5_result = a == 11 && b == (void *)12 && c == 13 && d == 14 &&
		e == 15;
	return 0;
}
