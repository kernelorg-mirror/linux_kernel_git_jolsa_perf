#ifndef __PERF_MEM_EVENTS_H
#define __PERF_MEM_EVENTS_H

#include <stdbool.h>

struct perf_mem_event {
	bool		record;
	bool		supported;
	const char	*tag;
	const char	*name;
	const char	*sysfs_name;
};

enum {
	PERF_MEM_EVENTS__LOAD,
	PERF_MEM_EVENTS__STORE,
	PERF_MEM_EVENTS__STLB_MISS_LOADS,
	PERF_MEM_EVENTS__STLB_MISS_STORES,
	PERF_MEM_EVENTS__LOCK_LOADS,
	PERF_MEM_EVENTS__SPLIT_LOADS,
	PERF_MEM_EVENTS__SPLIT_STORES,
	PERF_MEM_EVENTS__ALL_LOADS,
	PERF_MEM_EVENTS__ALL_STORES,
	PERF_MEM_EVENTS__L1_HIT,
	PERF_MEM_EVENTS__L2_HIT,
	PERF_MEM_EVENTS__L3_HIT,
	PERF_MEM_EVENTS__L1_MISS,
	PERF_MEM_EVENTS__L2_MISS,
	PERF_MEM_EVENTS__L3_MISS,
	PERF_MEM_EVENTS__LFB,
	PERF_MEM_EVENTS__SNP_MISS,
	PERF_MEM_EVENTS__SNP_HIT,
	PERF_MEM_EVENTS__SNP_HITM,
	PERF_MEM_EVENTS__SNP_NONE,
	PERF_MEM_EVENTS__LOCAL_DRAM,
	PERF_MEM_EVENTS__MAX,
};

extern struct perf_mem_event perf_mem_events[PERF_MEM_EVENTS__MAX];

int perf_mem_events__parse(const char *str);
int perf_mem_events__init(void);

char *perf_mem_events__name(int i);

struct mem_info;
int perf_mem__tlb_scnprintf(char *out, size_t sz, struct mem_info *mem_info);
int perf_mem__lvl_scnprintf(char *out, size_t sz, struct mem_info *mem_info);
int perf_mem__snp_scnprintf(char *out, size_t sz, struct mem_info *mem_info);
int perf_mem__lck_scnprintf(char *out, size_t sz, struct mem_info *mem_info);

int perf_script__meminfo_scnprintf(char *bf, size_t size, struct mem_info *mem_info);

#endif /* __PERF_MEM_EVENTS_H */
