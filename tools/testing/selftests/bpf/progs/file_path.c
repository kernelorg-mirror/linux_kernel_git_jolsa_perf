// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include "bpf_helpers.h"
#include "bpf_trace_helpers.h"

char _license[] SEC("license") = "GPL";

struct {
	__u64	my_pid_tgid;
	char	path[4096];
} data = {};

struct file;

BPF_TRACE_3("fentry/vfs_read", fentry_vfs_read,
            struct file *, file, void*, buf, void*, pos)
{
	__u64 pid_tgid = bpf_get_current_pid_tgid();
	int err;

	bpf_printk("krava1 %lu %lu\n", pid_tgid, data.my_pid_tgid);

	if (data.my_pid_tgid != pid_tgid)
		return 0;

	err = bpf_file_path(file, data.path, sizeof(data.path));
	return err > 0 ? 0 : -1;
}
