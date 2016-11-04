#ifndef __CODE_H
#define __CODE_H

#include <linux/bpf.h>
#include <stdbool.h>

struct unw_insn {
	struct bpf_insn	 bi;
	char		*cstr;
	char		*astr;
};

struct unw_code {
	struct unw_insn	*insn;
	int		 len;
	int		 alloc;
};

int add_code(struct unw_code *code, struct unw_insn *insn, int len);
int clean_code(struct unw_code *code, bool _free);

#endif /* __CODE_H */
