#ifndef __INTERP_H
#define __INTERP_H

#include <uapi/linux/bpf.h>
#include <linux/types.h>
#include <stdio.h>

struct bpf_interp;

typedef int (*bpf_interp_call_cb_t)(struct bpf_interp *interp,
				    u64 imm, u64 *regs);

struct bpf_interp {
	bpf_interp_call_cb_t	 call_cb;
	const struct bpf_insn	*insns;
	size_t			 insns_cnt;
	size_t			 insns_start;
};

u64 bpf_interp__run(struct bpf_interp *interp);

#endif /* __INTERP_H */
