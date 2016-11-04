#ifndef __EHFRAME_H
#define __EHFRAME_H

#include <libelf.h>

int eh_frame(Elf **_elf, int fd, unsigned long *start, unsigned long *stop);

extern unsigned long eh_frame_base;
extern unsigned long eh_frame_ptr;

#endif /* __EHFRAME_H */
