#include <stdlib.h>
#include <asm/errno.h>
#include <string.h>
#include "code.h"

#define CODE_ALLOC 100

int add_code(struct unw_code *code, struct unw_insn *insn, int len)
{
	int i;

	if (code->len + len >= code->alloc) {
		int alloc = code->alloc + CODE_ALLOC;
		struct unw_insn *insn;

		insn = realloc(code->insn, sizeof(*insn) * alloc);
		if (!insn)
			return -ENOMEM;

		code->insn  = insn;
		code->alloc = alloc;
	}

	memcpy(&code->insn[code->len], insn, sizeof(*insn) * len);
	code->len += len;
	return 0;
}

int clean_code(struct unw_code *code, bool _free)
{
	int i;

	for (i = 0; i < code->len; i++)
		free(code->insn[i].astr);

	code->len = 0;

	if (_free)
		free(code->insn);
}
