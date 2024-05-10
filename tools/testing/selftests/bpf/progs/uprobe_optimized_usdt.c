// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_tracing.h>
#include <bpf/usdt.bpf.h>

char _license[] SEC("license") = "GPL";
int executed = 0;

SEC("usdt")
int usdt0(struct pt_regs *ctx)
{
	executed = 1;
	return 0;
}
