#ifndef _DWARF_UNWIND_H
#define _DWARF_UNWIND_H

#include <uapi/linux/dwarf_unwind.h>

typedef int (du_entry)(struct pt_regs *, void *data);
int du_unwind_stack(struct pt_regs *regs, du_entry entry, void *data);

void du_arch_regs_get(struct du_regs *dr, struct pt_regs *pr);
void du_arch_regs_set(struct du_regs *dr, struct pt_regs *pr);
void du_arch_state_init(struct du_state_regs *state);

#endif /* _DWARF_UNWIND_H */
