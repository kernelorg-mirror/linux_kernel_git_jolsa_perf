#ifndef __PARSE_H
#define __PARSE_H

#include <linux/rbtree.h>

struct unw_frame {
	struct rb_node	 rb_node;

	u16	 ilen;
	u8	*icode;
};

struct unw_cie {
	struct unw_frame frame;

	u8	*addr;
	u8	 encoding;
	u8	 ret_addr_column;
	u8	 align_code;
	s8	 align_data;
	bool	 aug_z;
};

struct unw_fde {
	struct unw_frame frame;
	struct unw_cie	*cie;

	u8	*loc_start;
	u8	*loc_end;
};

typedef int (unw_fde_cb_t)(struct unw_fde *);

int parse_fdes(u8 *start, u8 *end);
int walk_fdes(unw_fde_cb_t cb);

#endif /* __PARSE_H */
