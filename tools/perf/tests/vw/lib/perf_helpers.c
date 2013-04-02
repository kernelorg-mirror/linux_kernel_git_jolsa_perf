#include <linux/compiler.h>
#include "perf_helpers.h"

void arch_adjust_domain(struct perf_event_attr *pe __maybe_unused)
{
#ifdef __arm__
	/* currently ARM.  FIXME: when Cortex A15 comes out */
	pe->exclude_user = 0;
	pe->exclude_kernel = 0;
	pe->exclude_hv = 0;

	pr_debug("Adjusting domain to 0,0,0 for ARM\n");
#endif
}
