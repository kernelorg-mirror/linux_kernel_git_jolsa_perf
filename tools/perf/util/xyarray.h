#ifndef _PERF_XYARRAY_H_
#define _PERF_XYARRAY_H_ 1

#include <sys/types.h>

struct xyarray {
	size_t row_size;
	size_t entry_size;
	size_t size;
	char contents[];
};

struct xyarray *xyarray__new(int xlen, int ylen, size_t entry_size);
void xyarray__delete(struct xyarray *xy);

static inline void *xyarray__entry(struct xyarray *xy, int x, int y)
{
	return &xy->contents[x * xy->row_size + y * xy->entry_size];
}

#define xyarray__for_each(array, entry)					\
	for (entry = (void *) &array->contents[0];			\
	     (void *) entry < ((void *) array->contents + array->size);	\
	     entry++)

#endif /* _PERF_XYARRAY_H_ */
