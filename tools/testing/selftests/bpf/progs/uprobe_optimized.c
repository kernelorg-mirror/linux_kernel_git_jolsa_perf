// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

SEC("uretprobe.multi")
int BPF_URETPROBE(test)
{
	bpf_printk("HIT\n");
	return 0;
}
