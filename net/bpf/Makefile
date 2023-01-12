# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_BPF_SYSCALL)	:= test_run.o
obj-$(CONFIG_BPF_SYSCALL)	+= test_func_2.o
obj-$(CONFIG_BPF_SYSCALL)	+= test_func_1.o
obj-$(CONFIG_BPF_SYSCALL)	+= test_func_3.o
ifeq ($(CONFIG_BPF_JIT),y)
obj-$(CONFIG_BPF_SYSCALL)	+= bpf_dummy_struct_ops.o
endif
