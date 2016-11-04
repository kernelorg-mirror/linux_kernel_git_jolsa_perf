#ifndef __PARSE_H
#define __PARSE_H

#include <linux/rbtree.h>

struct du_frame {
        struct rb_node rb_node;

        u16      ilen;
        u8      *icode;
};

struct du_cie {
        struct du_frame frame;

        u8      *addr;
        u8       encoding;
        u8       ret_addr_column;
        u8       align_code;
        s8       align_data;

        bool     aug_z;
};

struct du_fde {
        struct du_frame frame;

        struct du_cie   *cie;
        u8              *loc_start;
        u8              *loc_end;
};

int parse_limits(u8 *start, u8 *end);

extern unsigned long eh_frame_base;
extern unsigned long eh_frame_ptr;

typedef int(fde_cb_t)(struct du_fde *);
int walk_fdes(fde_cb_t cb);

#endif /* __PARSE_H */
