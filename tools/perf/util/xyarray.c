#include "xyarray.h"
#include "util.h"

struct xyarray *xyarray__new(int xlen, int ylen, size_t entry_size)
{
	size_t row_size = ylen * entry_size;
	size_t size = xlen * row_size;
	struct xyarray *xy = zalloc(sizeof(*xy) + size);

	if (xy != NULL) {
		xy->entry_size = entry_size;
		xy->row_size   = row_size;
		xy->size       = size;
	}

	return xy;
}

void xyarray__delete(struct xyarray *xy)
{
	free(xy);
}
