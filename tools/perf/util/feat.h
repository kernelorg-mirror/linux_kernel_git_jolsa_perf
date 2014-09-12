#ifndef __PERF_FEATURES_H
#define __PERF_FEATURES_H

#include <dlfcn.h>

struct perf_feature {
	const char	*name;
	const char	*lib;
	void		*addr;
};

enum {
	PERF_FEATURE__TEST = 0,
	PERF_FEATURE__MAX,
};

void* pf_resolve(unsigned int f, const char *symbol);

#define PF(pf, symbol) ({				\
	static typeof(symbol) *addr;			\
							\
	if (!addr)					\
		addr = pf_resolve(PERF_FEATURE__ ##pf, #symbol);	\
							\
	addr;						\
})

#endif /* __PERF_FEATURES_H */
