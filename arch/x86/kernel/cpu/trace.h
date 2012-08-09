#if !defined(_TRACE_X86_PERF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_X86_PERF_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM perf

#define BITS_TO_BYTES(bits) (bits / BITS_PER_BYTE)

/*
 * Class class for x86_pmu_enable/x86_pmu_disable tracepoints.
 */
DECLARE_EVENT_CLASS(x86_pmu_switch,

	TP_PROTO(int n, unsigned long *active),

	TP_ARGS(n, active),

	TP_STRUCT__entry(
		__field(	int,		n						)
		__array(	unsigned long,	active,	(int)BITS_TO_LONGS(X86_PMC_IDX_MAX)	)
	),

	TP_fast_assign(
		__entry->n = n;
		memcpy(__entry->active, active, BITS_TO_BYTES(X86_PMC_IDX_MAX));
	),

	TP_printk("events %d, active %s",
		  __entry->n,
		  __print_bitmap_bits(__entry->active,  X86_PMC_IDX_MAX))
);

/*
 * Tracepoint for x86 pmu_enable callback.
 */
DEFINE_EVENT(x86_pmu_switch, x86_pmu_enable,

	TP_PROTO(int n, unsigned long *active),

	TP_ARGS(n, active)
);

/*
 * Tracepoint for x86 pmu_disable callback.
 */
DEFINE_EVENT(x86_pmu_switch, x86_pmu_disable,

	TP_PROTO(int n, unsigned long *active),

	TP_ARGS(n, active)
);

#endif /* _TRACE_X86_PERF_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH arch/x86/kernel/cpu
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
