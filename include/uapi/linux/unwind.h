#ifndef _UAPI_LINUX_UNWIND_H
#define _UAPI_LINUX_UNWIND_H

#include <linux/types.h>
#include <linux/bpf.h>

struct unwind_frame {
	__u8		*loc_start;
	__u8		*loc_end;
	__u32		 len;
	struct bpf_insn	*insn;
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

enum du_arch_regs {
	/* Standard x86_64 registers. */
	DU_REG_X86_64_RAX,
	DU_REG_X86_64_RDX, 
	DU_REG_X86_64_RCX,
	DU_REG_X86_64_RBX,
	DU_REG_X86_64_RSI,
	DU_REG_X86_64_RDI,
	DU_REG_X86_64_RBP, 
	DU_REG_X86_64_RSP,
	DU_REG_X86_64_R8,
	DU_REG_X86_64_R9,
	DU_REG_X86_64_R10,
	DU_REG_X86_64_R11,
	DU_REG_X86_64_R12,
	DU_REG_X86_64_R13,
	DU_REG_X86_64_R14,
	DU_REG_X86_64_R15,
	DU_REG_X86_64_RIP,

	/* Trating CFA as special register. */
	DU_REG_CFA_REG_COLUMN,
	DU_REG_CFA_OFF_COLUMN,

	DU_REGS_NUM,

	DU_REG_SP  = DU_REG_X86_64_RSP,
	DU_REG_IP  = DU_REG_X86_64_RIP,
	DU_REG_CFA = DU_REG_CFA_REG_COLUMN,
};

struct du_state_reg {
	enum du_location loc;
	union {
		unsigned long val;
		struct {
			u8 *expr;
			unsigned long len;
		};
	};
};

struct du_state_regs {
	struct du_state_reg reg[DU_REGS_NUM];
};

#define DWARF_UNWIND_CFA_STACK_MAX 5

struct du_state {
	struct du_state_regs stack[DWARF_UNWIND_CFA_STACK_MAX * 2];
};

struct du_regs {
	unsigned long reg[DU_REGS_NUM];
};

struct du_unwind {
	unsigned long	ip;
	unsigned long	end;
	struct du_state	state;
};

#endif /* _UAPI_LINUX_UNWIND_H */
