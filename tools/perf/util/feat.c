#include <stdlib.h>
#include <linux/kernel.h>
#include "feat.h"
#include "debug.h"

struct perf_feature perf_features[PF_MAX] = {
	[PERF_FEATURE__TEST] = {
		.name	= "test",
		.lib	= "libaudit.so",
	},
};

void* pf_resolve(unsigned int f, const char *symbol)
{
	struct perf_feature *feat = &perf_features[f];
	void *handle, *addr = NULL;

	BUG_ON(f >= PF_MAX);

	handle = dlopen(feat->lib, RTLD_LAZY);
	if (handle)
		addr = dlsym(handle, symbol);

	return addr;
}
