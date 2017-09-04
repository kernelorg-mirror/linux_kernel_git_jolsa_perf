#ifndef __PYTHON_DUMP_H
#define __PYTHON_DUMP_H

typedef uint32_t u32;
typedef uint64_t u64;

struct python_header {
	u32	size;
	u32	id;
};

enum {
	PYTHON_DUMP__FUNC	= 1,
};

struct python_func {
	struct python_header	header;
	u64			start;
	u64			end;
	char			name[0];
};

#endif /* __PYTHON_DUMP_H */
