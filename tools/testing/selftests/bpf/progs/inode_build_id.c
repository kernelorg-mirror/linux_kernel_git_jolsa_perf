// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <linux/string.h>

char _license[] SEC("license") = "GPL";

int pid;

#define MAX_ERRNO 4095
#define IS_ERR_VALUE(x) (unsigned long)(void*)(x) >= (unsigned long)-MAX_ERRNO

static inline bool IS_ERR_OR_NULL(const void *ptr)
{
        return !ptr || IS_ERR_VALUE((unsigned long)ptr);
}

static int store_build_id(struct inode *inode, char *build_id, u32 *sz)
{
	struct build_id *bid;

	bid = inode->i_build_id;
	if (IS_ERR_OR_NULL(bid))
		return 0;
	*sz = bid->sz;
	if (bid->sz > sizeof(bid->data))
		return 0;
	memcpy(build_id, bid->data, sizeof(bid->data));
	return 0;
}

u32 build_id_bin_size;
char build_id_bin[20];

SEC("tp_btf/sched_process_exec")
int BPF_PROG(prog, struct task_struct *p, pid_t old_pid, struct linux_binprm *bprm)
{
	int cur_pid = bpf_get_current_pid_tgid() >> 32;

	if (pid != cur_pid)
		return 0;
	if (!bprm->file || !bprm->file->f_inode)
		return 0;
	return store_build_id(bprm->file->f_inode, build_id_bin, &build_id_bin_size);
}

u32 build_id_lib_size;
char build_id_lib[20];

static long check_vma(struct task_struct *task, struct vm_area_struct *vma,
		      void *data)
{
	if (!vma || !vma->vm_file || !vma->vm_file->f_inode)
		return 0;
	return store_build_id(vma->vm_file->f_inode, build_id_lib, &build_id_lib_size);
}

SEC("uprobe/liburandom_read.so:urandlib_read_without_sema")
int BPF_UPROBE(urandlib_read_without_sema)
{
	struct task_struct *task = bpf_get_current_task_btf();
	int cur_pid = bpf_get_current_pid_tgid() >> 32;

	if (pid != cur_pid)
		return 0;
	bpf_find_vma(task, ctx->ip, check_vma, NULL, 0);
	return 0;
}
