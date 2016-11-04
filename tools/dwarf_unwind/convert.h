#ifndef __CONVERT_H
#define __CONVERT_H

#include "parse.h"
#include "code.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

struct unw_convert {
	struct unw_code	*code;
	struct unw_code	*expr;
	int 		 expr_cnt;
	struct unw_fde	*fde;
	int		 state_idx;
};

int emit_cfi(struct unw_convert *c, struct unw_frame *frame);
int emit_expr(struct unw_code *code, u8 *addr, unsigned long len);
int emit_debug(struct unw_code *code);
int emit_next(struct unw_code *code);

#endif /* __CONVERT_H */

