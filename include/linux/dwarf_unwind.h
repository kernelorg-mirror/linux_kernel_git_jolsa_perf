#ifndef _DWARF_UNWIND_H
#define _DWARF_UNWIND_H

#include <uapi/linux/dwarf_unwind.h>

struct du_step_trace {
	struct pt_regs	 regs_in;
	struct pt_regs	 regs_out;
	struct du_frame	*frame;
	int		 cfi_ret;
	int		 ret;
	bool		 expr_cfa;
	unsigned long	 expr_cfa_ret;
	bool		 expr_reg;
	unsigned long	 expr_reg_val;
	unsigned long	 expr_reg_idx;
};

#endif /* _DWARF_UNWIND_H */
