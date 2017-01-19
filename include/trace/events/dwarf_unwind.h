#undef TRACE_SYSTEM
#define TRACE_SYSTEM dwarf_unwind

#if !defined(_TRACE_DWARF_UNWIND_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DWARF_UNWIND_H

#include <linux/dwarf_unwind.h>
#include <linux/tracepoint.h>

TRACE_EVENT(dwarf_unwind_step,

	TP_PROTO(struct du_step_trace *trace),

	TP_ARGS(trace),

	TP_STRUCT__entry(
		__field(void *		, loc_start	)
		__field(int		, cfi_ret	)
		__field(int		, ret		)
		__field(unsigned long	, ip_in		)
		__field(unsigned long	, sp_in		)
		__field(unsigned long	, ip_out	)
		__field(unsigned long	, sp_out	)
		__field(int		, expr_cfa	)
		__field(unsigned long	, expr_cfa_ret	)
		__field(int		, expr_reg	)
		__field(int		, expr_reg_idx	)
		__field(unsigned long	, expr_reg_val	)
	),

	TP_fast_assign(
		__entry->loc_start = trace->frame ? trace->frame->loc_start : 0;
		__entry->cfi_ret      = trace->cfi_ret;
		__entry->ret          = trace->ret;
		__entry->ip_in        = trace->regs_in.ip;
		__entry->sp_in        = trace->regs_in.sp;
		__entry->ip_out       = trace->regs_out.ip;
		__entry->sp_out       = trace->regs_out.sp;
		__entry->expr_cfa     = trace->expr_cfa;
		__entry->expr_cfa_ret = trace->expr_cfa_ret;
		__entry->expr_reg     = trace->expr_reg;
		__entry->expr_reg_val = trace->expr_reg_val;
		__entry->expr_reg_idx = trace->expr_reg_idx;
	),

	TP_printk("f %p, cfi ret %5d, ret %5d, IN(ip %p, sp %p), OUT(ip %p, sp %p), ECFA(%d, %p), EREG(%d, %d, %p)",
		__entry->loc_start,
		__entry->cfi_ret,
		__entry->ret,
		(void *) __entry->ip_in,
		(void *) __entry->sp_in,
		(void *) __entry->ip_out,
		(void *) __entry->sp_out,
		__entry->expr_cfa,
		(void *) __entry->expr_cfa_ret,
		__entry->expr_reg,
		__entry->expr_reg_idx,
		(void *) __entry->expr_reg_val
	)
);

#endif /* _TRACE_DWARF_UNWIND_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
