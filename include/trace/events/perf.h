#if !defined(_TRACE_PERF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PERF_H

#include <linux/tracepoint.h>
#include <linux/types.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM perf

#define BITS_TO_BYTES(bits) (bits / BITS_PER_BYTE)

/*
 * Tracepoint for sys_perf_event_open syscall.
 */
TRACE_EVENT(sys_perf_event_open,

	TP_PROTO(u64 id, int fd, pid_t pid, int cpu,
		 int group_fd, unsigned long flags),

	TP_ARGS(id, fd, pid, cpu, group_fd, flags),

	TP_STRUCT__entry(
		__field(	u64,		id		)
		__field(	int,		fd		)
		__field(	pid_t,		pid		)
		__field(	int,		cpu		)
		__field(	int,		group_fd	)
		__field(	unsigned long,	flags		)
	),

	TP_fast_assign(
		__entry->fd = fd;
		__entry->id = id;
		__entry->pid = pid;
		__entry->pid = pid;
		__entry->cpu = cpu;
		__entry->group_fd = group_fd;
		__entry->flags = flags;
	),

	TP_printk("id %llu, fd %d, pid %d, cpu %d, group_fd %d, flags %lu",
		  __entry->id, __entry->fd, __entry->pid, __entry->cpu,
		  __entry->group_fd, __entry->flags)
);

/*
 * Class for event id only perf tracepoints.
 */
DECLARE_EVENT_CLASS(perf_id,

	TP_PROTO(u64 id),

	TP_ARGS(id),

	TP_STRUCT__entry(
		__field(	u64,	id	)
	),

	TP_fast_assign(
		__entry->id = id;
	),

	TP_printk("id %llu", __entry->id)
);

/*
 * Tracepoint for event_enable_on_exec.
 */
DEFINE_EVENT(perf_id, event_enable_on_exec,

	TP_PROTO(u64 id),

	TP_ARGS(id)
);

/*
 * Tracepoint for perf_event_enable.
 */
DEFINE_EVENT(perf_id, perf_event_enable,

	TP_PROTO(u64 id),

	TP_ARGS(id)
);

/*
 * Tracepoint for perf_event_disable.
 */
DEFINE_EVENT(perf_id, perf_event_disable,

	TP_PROTO(u64 id),

	TP_ARGS(id)
);

/*
 * Tracepoint for event_sched_in.
 */
DEFINE_EVENT(perf_id, event_sched_in,

	TP_PROTO(u64 id),

	TP_ARGS(id)
);

/*
 * Tracepoint for event_sched_out.
 */
DEFINE_EVENT(perf_id, event_sched_out,

	TP_PROTO(u64 id),

	TP_ARGS(id)
);

/*
 * Tracepoint for event_sched_out.
 */
DEFINE_EVENT(perf_id, perf_event_output,

	TP_PROTO(u64 id),

	TP_ARGS(id)
);

#endif /* _TRACE_PERF_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
