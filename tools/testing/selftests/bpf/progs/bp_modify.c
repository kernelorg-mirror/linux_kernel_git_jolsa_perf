// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/usdt.bpf.h>

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(__u32));
} events SEC(".maps");

char _license[] SEC("license") = "GPL";

extern int bpf_bp_modify_addr(const struct bpf_map *map, u64 flags, unsigned long addr) __ksym __weak;

SEC("tp_btf/sched_switch")
int BPF_PROG(sched_switch, bool preempt, struct task_struct *prev,
	     struct task_struct *next, unsigned int prev_state)
{
	struct bpf_map *map = (struct bpf_map *)&events;
	unsigned long addr;

	addr = (unsigned long) __builtin_preserve_access_index(&next->cred);
	bpf_bp_modify_addr(map, BPF_F_CURRENT_CPU, addr);
	return 0;
}

SEC("perf_event")
void overflow(struct bpf_perf_event_data *data)
{
	bpf_printk("OVERFLOW ip %lx\n", PT_REGS_IP(&data->regs));
}
