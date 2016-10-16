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

#endif /* _UAPI_LINUX_UNWIND_H */
