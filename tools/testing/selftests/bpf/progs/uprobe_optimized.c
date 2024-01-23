// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";
int executed = 0;

SEC("uprobe.multi")
int BPF_UPROBE(test)
{
	executed = 1;
	return 0;
}
