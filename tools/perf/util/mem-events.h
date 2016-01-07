#ifndef __PERF_MEM_EVENTS_H
#define __PERF_MEM_EVENTS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <linux/types.h>

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
extern unsigned int perf_mem_events__loads_ldlat;

int perf_mem_events__parse(const char *str);
int perf_mem_events__init(void);

char *perf_mem_events__name(int i);

struct mem_info;
int perf_mem__tlb_scnprintf(char *out, size_t sz, struct mem_info *mem_info);
int perf_mem__lvl_scnprintf(char *out, size_t sz, struct mem_info *mem_info);
int perf_mem__snp_scnprintf(char *out, size_t sz, struct mem_info *mem_info);
int perf_mem__lck_scnprintf(char *out, size_t sz, struct mem_info *mem_info);

int perf_script__meminfo_scnprintf(char *bf, size_t size, struct mem_info *mem_info);

typedef struct {
	int  locks;               /* count of 'lock' transactions */
	int  store;               /* count of all stores in trace */
	int  st_uncache;          /* stores to uncacheable address */
	int  st_noadrs;           /* cacheable store with no address */
	int  st_l1hit;            /* count of stores that hit L1D */
	int  st_l1miss;           /* count of stores that miss L1D */
	int  load;                /* count of all loads in trace */
	int  ld_excl;             /* exclusive loads, rmt/lcl DRAM - snp none/miss */
	int  ld_shared;           /* shared loads, rmt/lcl DRAM - snp hit */
	int  ld_uncache;          /* loads to uncacheable address */
	int  ld_io;               /* loads to io address */
	int  ld_miss;             /* loads miss */
	int  ld_noadrs;           /* cacheable load with no address */
	int  ld_fbhit;            /* count of loads hitting Fill Buffer */
	int  ld_l1hit;            /* count of loads that hit L1D */
	int  ld_l2hit;            /* count of loads that hit L2D */
	int  ld_llchit;           /* count of loads that hit LLC */
	int  lcl_hitm;            /* count of loads with local HITM  */
	int  rmt_hitm;            /* count of loads with remote HITM */
	int  rmt_hit;             /* count of loads with remote hit clean; */
	int  lcl_dram;            /* count of loads miss to local DRAM */
	int  rmt_dram;            /* count of loads miss to remote DRAM */
	int  nomap;               /* count of load/stores with no phys adrs */
	int  noparse;             /* count of unparsable data sources */
} trinfo_t;

struct c2c_stats {
	int		nr_entries;
	u64		total_period;
	trinfo_t	t;
};

struct hist_entry;
int c2c_decode_stats(struct c2c_stats *stats, struct hist_entry *entry);

#endif /* __PERF_MEM_EVENTS_H */
