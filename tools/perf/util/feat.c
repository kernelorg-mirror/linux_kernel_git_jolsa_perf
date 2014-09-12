#include <stdlib.h>
#include <stdbool.h>
#include <linux/kernel.h>
#include "feat.h"
#include "debug.h"
#include "asm/bug.h"

struct perf_feature perf_features[PF_MAX] = {
	[PERF_FEATURE__TEST] = {
		.name	= "test",
		.lib	= "libc.so.6",
		.in	= 1,
	},
	[PERF_FEATURE__UNW] = {
		.name	= "DWARF unwind",
#ifdef HAVE_LIBUNWIND_SUPPORT
		.lib	= PF_LIBUNWIND,
		.in	= 1,
#endif
	},
};

static void nop(void)
{
	printf("failed to resolve features symbol\n");
}

struct perf_feature *pf_get(unsigned int f)
{
	struct perf_feature *feat = &perf_features[f];

	if (WARN_ONCE(f >= PF_MAX, "unsupported feature"))
		return NULL;

	if (feat->in && !feat->handle) {
		feat->handle = dlopen(feat->lib, RTLD_LAZY);

		if (!feat->handle) {
			char buf[PATH_MAX];

			scnprintf(buf, sizeof(buf), "%s/%s", LIBDIR, feat->lib);
			feat->handle = dlopen(buf, RTLD_LAZY);
		}
	}

	return feat;
}

void* pf_resolve(unsigned int f, const char *symbol)
{
	struct perf_feature *feat = pf_get(f);
	void *addr = NULL;

	if (feat)
		addr = dlsym(feat->handle, symbol);

	return addr ?: nop;
}

bool pf_has(unsigned int f)
{
	struct perf_feature *feat = pf_get(f);

	return feat ? feat->handle : false;
}
