// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

extern const void bpf_fentry_test1 __ksym;
extern const void bpf_fentry_test2 __ksym;
extern const void bpf_fentry_test3 __ksym;
extern const void bpf_fentry_test4 __ksym;
extern const void bpf_fentry_test5 __ksym;
extern const void bpf_fentry_test6 __ksym;
extern const void bpf_fentry_test7 __ksym;
extern const void bpf_fentry_test8 __ksym;

int pid = 0;

/* No tests, just to trigger bpf_fentry_test* through tracing test_run */
SEC("fentry/bpf_modify_return_test")
int BPF_PROG(trigger)
{
	return 0;
}

__u64 kprobe_test1_result = 0;
__u64 kprobe_test2_result = 0;
__u64 kprobe_test3_result = 0;
__u64 kprobe_test4_result = 0;
__u64 kprobe_test5_result = 0;
__u64 kprobe_test6_result = 0;
__u64 kprobe_test7_result = 0;
__u64 kprobe_test8_result = 0;

SEC("kprobe.multi/bpf_fentry_tes??")
int test_kprobe(struct pt_regs *ctx)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	__u64 cookie = bpf_get_attach_cookie(ctx);
	__u64 addr = bpf_get_func_ip(ctx);

#define SET(__var, __addr, __cookie) ({			\
	if ((const void *) addr == __addr && cookie == __cookie)\
		__var = 1;				\
})

	SET(kprobe_test1_result, &bpf_fentry_test1, 1);
	SET(kprobe_test2_result, &bpf_fentry_test2, 2);
	SET(kprobe_test3_result, &bpf_fentry_test3, 3);
	SET(kprobe_test4_result, &bpf_fentry_test4, 4);
	SET(kprobe_test5_result, &bpf_fentry_test5, 5);
	SET(kprobe_test6_result, &bpf_fentry_test6, 6);
	SET(kprobe_test7_result, &bpf_fentry_test7, 7);
	SET(kprobe_test8_result, &bpf_fentry_test8, 8);

	return 0;
}

__u64 kretprobe_test1_result = 0;
__u64 kretprobe_test2_result = 0;
__u64 kretprobe_test3_result = 0;
__u64 kretprobe_test4_result = 0;
__u64 kretprobe_test5_result = 0;
__u64 kretprobe_test6_result = 0;
__u64 kretprobe_test7_result = 0;
__u64 kretprobe_test8_result = 0;

SEC("kretprobe.multi/bpf_fentry_test*")
int test_kretprobe(struct pt_regs *ctx)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	__u64 cookie = bpf_get_attach_cookie(ctx);
	__u64 addr = bpf_get_func_ip(ctx);

	SET(kretprobe_test1_result, &bpf_fentry_test1, 8);
	SET(kretprobe_test2_result, &bpf_fentry_test2, 7);
	SET(kretprobe_test3_result, &bpf_fentry_test3, 6);
	SET(kretprobe_test4_result, &bpf_fentry_test4, 5);
	SET(kretprobe_test5_result, &bpf_fentry_test5, 4);
	SET(kretprobe_test6_result, &bpf_fentry_test6, 3);
	SET(kretprobe_test7_result, &bpf_fentry_test7, 2);
	SET(kretprobe_test8_result, &bpf_fentry_test8, 1);

	return 0;
}
