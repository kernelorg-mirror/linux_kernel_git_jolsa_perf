// SPDX-License-Identifier: GPL-2.0
#include <linux/perf_event.h>
#include <linux/sysfs.h>
#include <linux/nospec.h>
#include <asm/intel-family.h>
#include "probe.h"

enum perf_msr_id {
	PERF_MSR_TSC			= 0,
	PERF_MSR_APERF			= 1,
	PERF_MSR_MPERF			= 2,
	PERF_MSR_PPERF			= 3,
	PERF_MSR_SMI			= 4,
	PERF_MSR_PTSC			= 5,
	PERF_MSR_IRPERF			= 6,
	PERF_MSR_THERM			= 7,
	PERF_MSR_EVENT_MAX,
};

static bool test_aperfmperf(int idx, void *data)
{
	return boot_cpu_has(X86_FEATURE_APERFMPERF);
}

static bool test_ptsc(int idx, void *data)
{
	return boot_cpu_has(X86_FEATURE_PTSC);
}

static bool test_irperf(int idx, void *data)
{
	return boot_cpu_has(X86_FEATURE_IRPERF);
}

static bool test_therm_status(int idx, void *data)
{
	return boot_cpu_has(X86_FEATURE_DTHERM);
}

static bool test_intel(int idx, void *data)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL ||
	    boot_cpu_data.x86 != 6)
		return false;

	switch (boot_cpu_data.x86_model) {
	case INTEL_FAM6_NEHALEM:
	case INTEL_FAM6_NEHALEM_G:
	case INTEL_FAM6_NEHALEM_EP:
	case INTEL_FAM6_NEHALEM_EX:

	case INTEL_FAM6_WESTMERE:
	case INTEL_FAM6_WESTMERE_EP:
	case INTEL_FAM6_WESTMERE_EX:

	case INTEL_FAM6_SANDYBRIDGE:
	case INTEL_FAM6_SANDYBRIDGE_X:

	case INTEL_FAM6_IVYBRIDGE:
	case INTEL_FAM6_IVYBRIDGE_X:

	case INTEL_FAM6_HASWELL_CORE:
	case INTEL_FAM6_HASWELL_X:
	case INTEL_FAM6_HASWELL_ULT:
	case INTEL_FAM6_HASWELL_GT3E:

	case INTEL_FAM6_BROADWELL_CORE:
	case INTEL_FAM6_BROADWELL_XEON_D:
	case INTEL_FAM6_BROADWELL_GT3E:
	case INTEL_FAM6_BROADWELL_X:

	case INTEL_FAM6_ATOM_SILVERMONT:
	case INTEL_FAM6_ATOM_SILVERMONT_X:
	case INTEL_FAM6_ATOM_AIRMONT:

	case INTEL_FAM6_ATOM_GOLDMONT:
	case INTEL_FAM6_ATOM_GOLDMONT_X:

	case INTEL_FAM6_ATOM_GOLDMONT_PLUS:

	case INTEL_FAM6_XEON_PHI_KNL:
	case INTEL_FAM6_XEON_PHI_KNM:
		if (idx == PERF_MSR_SMI)
			return true;
		break;

	case INTEL_FAM6_SKYLAKE_MOBILE:
	case INTEL_FAM6_SKYLAKE_DESKTOP:
	case INTEL_FAM6_SKYLAKE_X:
	case INTEL_FAM6_KABYLAKE_MOBILE:
	case INTEL_FAM6_KABYLAKE_DESKTOP:
	case INTEL_FAM6_ICELAKE_MOBILE:
		if (idx == PERF_MSR_SMI || idx == PERF_MSR_PPERF)
			return true;
		break;
	}

	return false;
}

PMU_EVENT_ATTR_STRING(tsc,				tsc_attr,		"event=0x00"	);
PMU_EVENT_ATTR_STRING(aperf,				aperf_attr,		"event=0x01"	);
PMU_EVENT_ATTR_STRING(mperf,				mperf_attr,		"event=0x02"	);
PMU_EVENT_ATTR_STRING(pperf,				pperf_attr,		"event=0x03"	);
PMU_EVENT_ATTR_STRING(smi,				smi_attr,		"event=0x04"	);
PMU_EVENT_ATTR_STRING(ptsc,				ptsc_attr,		"event=0x05"	);
PMU_EVENT_ATTR_STRING(irperf,				irperf_attr,		"event=0x06"	);
PMU_EVENT_ATTR_STRING(cpu_thermal_margin,		therm_attr,		"event=0x07"	);
PMU_EVENT_ATTR_STRING(cpu_thermal_margin.snapshot,	therm_snap_attr,	"1"		);
PMU_EVENT_ATTR_STRING(cpu_thermal_margin.unit,		therm_unit_attr,	"C"		);

static unsigned long msr_mask;

#define __ATTRIBUTE_GROUP(_name)			\
static struct attribute *_name##_attrs[] = {		\
	&_name##_attr.attr.attr,			\
	NULL,						\
};

#define ATTRIBUTE_GROUP(_name)				\
__ATTRIBUTE_GROUP(_name)				\
static struct attribute_group _name##_group = {		\
	.name  = "events",				\
	.attrs = _name##_attrs,				\
};							\

ATTRIBUTE_GROUP(tsc);
ATTRIBUTE_GROUP(aperf);
ATTRIBUTE_GROUP(mperf);
ATTRIBUTE_GROUP(pperf);
ATTRIBUTE_GROUP(smi);
ATTRIBUTE_GROUP(ptsc);
ATTRIBUTE_GROUP(irperf);

static struct attribute *therm_attrs[] = {
	&therm_attr.attr.attr,
	&therm_snap_attr.attr.attr,
	&therm_unit_attr.attr.attr,
	NULL,
};

static struct attribute_group therm_group = {
	.name  = "events",
	.attrs = therm_attrs,
};

static struct perf_msr msr[] = {
	[PERF_MSR_TSC]		= { 0,				&tsc_group,		NULL,			},
	[PERF_MSR_APERF]	= { MSR_IA32_APERF,		&aperf_group,		test_aperfmperf,	},
	[PERF_MSR_MPERF]	= { MSR_IA32_MPERF,		&mperf_group,		test_aperfmperf,	},
	[PERF_MSR_PPERF]	= { MSR_PPERF,			&pperf_group,		test_intel,		},
	[PERF_MSR_SMI]		= { MSR_SMI_COUNT,		&smi_group,		test_intel,		},
	[PERF_MSR_PTSC]		= { MSR_F15H_PTSC,		&ptsc_group,		test_ptsc,		},
	[PERF_MSR_IRPERF]	= { MSR_F17H_IRPERF,		&irperf_group,		test_irperf,		},
	[PERF_MSR_THERM]	= { MSR_IA32_THERM_STATUS,	&therm_group,	test_therm_status,	},
};

static struct attribute *events_attrs[PERF_MSR_EVENT_MAX + 1] = {
	NULL,
};

static struct attribute_group events_attr_group = {
	.name = "events",
	.attrs = events_attrs,
};

PMU_FORMAT_ATTR(event, "config:0-63");
static struct attribute *format_attrs[] = {
	&format_attr_event.attr,
	NULL,
};
static struct attribute_group format_attr_group = {
	.name = "format",
	.attrs = format_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&events_attr_group,
	&format_attr_group,
	NULL,
};

static int msr_event_init(struct perf_event *event)
{
	u64 cfg = event->attr.config;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* unsupported modes and filters */
	if (event->attr.sample_period) /* no sampling */
		return -EINVAL;

	if (cfg >= PERF_MSR_EVENT_MAX)
		return -EINVAL;

	cfg = array_index_nospec((unsigned long)cfg, PERF_MSR_EVENT_MAX);

	if (!(msr_mask & (1 << cfg)))
		return -EINVAL;

	event->hw.idx		= -1;
	event->hw.event_base	= msr[cfg].msr;
	event->hw.config	= cfg;

	return 0;
}

static inline u64 msr_read_counter(struct perf_event *event)
{
	u64 now;

	if (event->hw.event_base)
		rdmsrl(event->hw.event_base, now);
	else
		now = rdtsc_ordered();

	return now;
}

static void msr_event_update(struct perf_event *event)
{
	u64 prev, now;
	s64 delta;

	/* Careful, an NMI might modify the previous event value: */
again:
	prev = local64_read(&event->hw.prev_count);
	now = msr_read_counter(event);

	if (local64_cmpxchg(&event->hw.prev_count, prev, now) != prev)
		goto again;

	delta = now - prev;
	if (unlikely(event->hw.event_base == MSR_SMI_COUNT)) {
		delta = sign_extend64(delta, 31);
		local64_add(delta, &event->count);
	} else if (unlikely(event->hw.event_base == MSR_IA32_THERM_STATUS)) {
		/* If valid, extract digital readout, otherwise set to -1: */
		now = now & (1ULL << 31) ? (now >> 16) & 0x3f :  -1;
		local64_set(&event->count, now);
	} else {
		local64_add(delta, &event->count);
	}
}

static void msr_event_start(struct perf_event *event, int flags)
{
	u64 now = msr_read_counter(event);

	local64_set(&event->hw.prev_count, now);
}

static void msr_event_stop(struct perf_event *event, int flags)
{
	msr_event_update(event);
}

static void msr_event_del(struct perf_event *event, int flags)
{
	msr_event_stop(event, PERF_EF_UPDATE);
}

static int msr_event_add(struct perf_event *event, int flags)
{
	if (flags & PERF_EF_START)
		msr_event_start(event, flags);

	return 0;
}

static struct pmu pmu_msr = {
	.task_ctx_nr	= perf_sw_context,
	.attr_groups	= attr_groups,
	.event_init	= msr_event_init,
	.add		= msr_event_add,
	.del		= msr_event_del,
	.start		= msr_event_start,
	.stop		= msr_event_stop,
	.read		= msr_event_update,
	.capabilities	= PERF_PMU_CAP_NO_INTERRUPT | PERF_PMU_CAP_NO_EXCLUDE,
};

static int __init msr_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_TSC)) {
		pr_cont("no MSR PMU driver.\n");
		return 0;
	}

	msr[PERF_MSR_TSC].no_check = true;
	msr_mask = perf_msr_probe(msr, PERF_MSR_EVENT_MAX, NULL);

	perf_pmu_register(&pmu_msr, "msr", -1);

	return 0;
}
device_initcall(msr_init);
