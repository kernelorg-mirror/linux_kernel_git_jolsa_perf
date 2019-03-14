/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_X86_EVENTS_PROBE_H__
#define __ARCH_X86_EVENTS_PROBE_H__
#include <linux/sysfs.h>

struct perf_msr {
	u64			  msr;
	struct attribute_group	 *grp;
	bool			(*test)(int idx, void *data);
	bool			  no_check;
};

unsigned long
perf_msr_probe(struct perf_msr *msr, int cnt, void *data);

#define __PMU_EVENT_GROUP(_name)			\
static struct attribute *_name##_attrs[] = {		\
	&_name##_attr.attr.attr,			\
	NULL,						\
}

#define PMU_EVENT_GROUP(_grp, _name)			\
__PMU_EVENT_GROUP(_name);				\
static struct attribute_group _name##_group = {		\
	.name  = #_grp,					\
	.attrs = _name##_attrs,				\
}

#endif /* __ARCH_X86_EVENTS_PROBE_H__ */
