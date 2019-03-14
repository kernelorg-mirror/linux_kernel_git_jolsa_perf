/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_X86_EVENTS_PROBE_H__
#define __ARCH_X86_EVENTS_PROBE_H__
#include <linux/sysfs.h>

#define MSR_ATTR(__n)				\
static struct attribute *msr_##__n[] = {	\
	&__n.attr.attr,				\
	NULL,					\
}

struct perf_msr {
	u64			  msr;
	struct attribute_group	 *grp;
	bool			(*test)(int idx, void *data);
	bool			  no_check;
};

unsigned long
perf_msr_probe(struct perf_msr *msr, int cnt, void *data);
#endif /* __ARCH_X86_EVENTS_PROBE_H__ */
