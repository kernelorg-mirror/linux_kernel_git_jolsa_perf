#ifndef __PERF_FEATURES_H
#define __PERF_FEATURES_H

#include <dlfcn.h>

struct perf_feature {
	const char	*name;
	const char	*lib;
	void		*handle;
	bool		 in;
};

enum {
	PERF_FEATURE__TEST = 0,
	PERF_FEATURE__UNW,
	PERF_FEATURE__NUMA,
	PERF_FEATURE__MAX,
};

#define ____string(s) __string(s)
#define __string(s) #s

void* pf_resolve(unsigned int f, const char *symbol);

#define PF(pf, symbol) ({				\
	static typeof(symbol) *addr;			\
							\
	if (!addr)					\
		addr = pf_resolve(PERF_FEATURE__ ##pf, __string(symbol));       \
							\
	addr;						\
})

#define PF_HAS(pf) pf_has(PERF_FEATURE__ ##pf)

struct perf_feature *pf_get(unsigned int f);
bool pf_has(unsigned int f);

#endif /* __PERF_FEATURES_H */
