// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

SEC("fentry/bpf_test_func")
int BPF_PROG(prog, struct bpf_prog *prog)
{
	bpf_printk("KRAVA %p %p\n", prog, prog->aux);
	return 0;
}

char _license[] SEC("license") = "GPL";
