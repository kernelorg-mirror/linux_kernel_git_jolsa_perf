// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "uprobe_bench.skel.h"

noinline void uprobe1_bench_trigger(void)
{
        asm volatile ("");
}

/*
 * Assuming following prolog:
 *
 * 6984ac:       55                      push   %rbp
 * 6984ad:       48 89 e5                mov    %rsp,%rbp
 */
noinline void uprobe2_bench_trigger(void)
{
        asm volatile ("");
}

static inline int sys_bpf(enum bpf_cmd cmd, union bpf_attr *attr,
			  unsigned int size)
{
	return syscall(__NR_bpf, cmd, attr, size);
}

#define LOOPS 100000000

static void test_bench_1(void)
{
	double delta_uprobe1, delta_uprobe2, delta_sys;
	long uprobe1_start_ns, uprobe1_end_ns;
	long uprobe2_start_ns, uprobe2_end_ns;
	long sys_start_ns, sys_end_ns;
	struct uprobe_bench *skel;
	union bpf_attr attr = {};
	int err, i;

	skel = uprobe_bench__open_and_load();
	if (!ASSERT_OK_PTR(skel, "uprobe_bench__open_and_load"))
		goto cleanup;

	err = uprobe_bench__attach(skel);
	if (!ASSERT_OK(err, "uprobe_bench__attach"))
		goto cleanup;

	// uprobe on uprobe1_bench_trigger
	uprobe1_start_ns = get_time_ns();

	for (i = 0; i < LOOPS; i++) {
		uprobe1_bench_trigger();
	}

	uprobe1_end_ns = get_time_ns();

	ASSERT_EQ(skel->bss->uprobe1_cnt, LOOPS, "uprobe1_cnt");
	skel->bss->uprobe1_cnt = 0;

	// syscall on uprobe1_bench_trigger
	attr.uprobe.vaddr = (__u64) uprobe1_bench_trigger;

	sys_start_ns = get_time_ns();

	for (i = 0; i < LOOPS; i++) {
		err = sys_bpf(BPF_UPROBE, &attr, sizeof(attr));
		if (err)
			fprintf(stderr, "syscall failed err %d\n", err);
	}

	sys_end_ns = get_time_ns();

	ASSERT_EQ(skel->bss->uprobe1_cnt, LOOPS, "syscalls_uprobe1_cnt");

	// uprobe on uprobe2_bench_trigger+1
	uprobe2_start_ns = get_time_ns();

	for (i = 0; i < LOOPS; i++) {
		uprobe2_bench_trigger();
	}

	uprobe2_end_ns = get_time_ns();

	ASSERT_EQ(skel->bss->uprobe2_cnt, LOOPS, "uprobe2_cnt");

	delta_uprobe1 = (uprobe1_end_ns - uprobe1_start_ns) / 1000000000.0;
	delta_uprobe2 = (uprobe2_end_ns - uprobe2_start_ns) / 1000000000.0;
	delta_sys = (sys_end_ns - sys_start_ns) / 1000000000.0;

	printf("%s: uprobes (1 trap) in %7.3lfs\n", __func__, delta_uprobe1);
	printf("%s: uprobes (2 trap) in %7.3lfs\n", __func__, delta_uprobe2);
	printf("%s: syscalls         in %7.3lfs\n", __func__, delta_sys);

cleanup:
	uprobe_bench__destroy(skel);
}

void test_uprobe_syscall_bench(void)
{
	if (test__start_subtest("bench_1"))
		test_bench_1();
}
