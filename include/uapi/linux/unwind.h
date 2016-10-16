#ifndef _UAPI_LINUX_UNWIND_H
#define _UAPI_LINUX_UNWIND_H

#include <linux/types.h>

struct unwind_frame {
	__u8	*loc_start;
	__u8	*loc_end;
	__u32	 code;
	__u32	 len;
};

struct unwind_data {
	__u32	 version;
	__u32	 code;

	struct unwind_frame frames[0];
};

#endif /* _UAPI_LINUX_UNWIND_H */
