#include <errno.h>
#include "perf_regs.h"

int perf_reg_value(u64 *valp, struct regs_dump *regs, int id,
		   u64 sample_regs)
{
	int i, idx = 0;

	if (!(sample_regs & (1 << id)))
		return -EINVAL;

	for (i = 0; i < id; i++) {
		if (sample_regs & (1 << i))
			idx++;
	}

	*valp = regs->regs[idx];
	return 0;
}
