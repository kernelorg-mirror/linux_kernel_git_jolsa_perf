#ifndef _UAPI__DWARF_UNWIND_H
#define _UAPI__DWARF_UNWIND_H

#include <linux/types.h>
#include <linux/bpf.h>
#include <asm/dwarf_unwind.h>
#include <asm/ptrace.h>

struct du_expr {
	__u32		 len;
	struct bpf_insn	*insn;
};

struct du_expr_array {
	int		 len;
	struct du_expr	*expr[];
};

struct du_frame {
	__u8			*loc_start;
	__u8			*loc_end;
	__u32			 len;
	struct bpf_insn		*insn;
	struct du_expr_array	*expr;
};

enum du_location {
	DU_LOCATION_SAME = 0,
	DU_LOCATION_UNDEF,
	DU_LOCATION_REG,
	DU_LOCATION_MEMORY,
	DU_LOCATION_VALUE,
	DU_LOCATION_EXPR,
	DU_LOCATION_EXPR_VALUE,
};

struct du_state_reg {
	__u64	loc;
	__u64	val;
};

struct du_state_regs {
	struct du_state_reg	reg[DU_REGS_NUM];
};

#define DU_CFA_STACK_MAX 10

struct du_state {
	struct du_state_regs	stack[DU_CFA_STACK_MAX];
};

struct du_regs {
	unsigned long	reg[DU_REGS_NUM];
};

struct du_unwind {
	unsigned long	ip;
	unsigned long 	end;
	struct du_state	state;
};

struct du_int_expr {
	unsigned long	 val;
	struct du_regs	*regs;
};

void du_arch_regs_get(struct du_regs *dr, struct pt_regs *pr);
void du_arch_regs_set(struct du_regs *dr, struct pt_regs *pr);

#endif /* _UAPI__DWARF_UNWIND_H */
