// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

__u64 test1_result = 0;
__u64 test2_result = 0;
__u64 test1_ret_result = 0;
__u64 test2_ret_result = 0;

__hidden extern void multi_arg_check(__u64 *ctx, __u64 *test_result);
__hidden extern void multi_ret_check(void *ctx, __u64 *test_result);

SEC("fexit.multi")
int BPF_PROG(prog1)
{
	multi_arg_check(ctx, &test1_result);
	multi_ret_check(ctx, &test1_ret_result);
	return 0;
}

SEC("fexit.multi")
int BPF_PROG(prog2)
{
	multi_arg_check(ctx, &test2_result);
	multi_ret_check(ctx, &test2_ret_result);
	return 0;
}
