/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_SYS_H
#define _PERF_SYS_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <linux/compiler.h>

struct perf_event_attr;

extern bool test_attr__enabled;
void test_attr__ready(void);
void test_attr__init(void);
void test_attr__open(struct perf_event_attr *attr, pid_t pid, int cpu,
		     int fd, int group_fd, unsigned long flags);

static inline int
sys_perf_event_open(struct perf_event_attr *attr,
		      pid_t pid, int cpu, int group_fd,
		      unsigned long flags)
{
	return syscall(__NR_perf_event_open, attr, pid, cpu,
		     group_fd, flags);
}

#endif /* _PERF_SYS_H */
