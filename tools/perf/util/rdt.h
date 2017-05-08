#ifndef __PERF_RDT_H
#define __PERF_RDT_H

#include <linux/list.h>
#include <linux/types.h>
#include <cpumap.h>

enum {
	RDT_RESOURCE_L3,
	RDT_RESOURCE_L3DATA,
	RDT_RESOURCE_L3CODE,
	RDT_RESOURCE_L2,
	RDT_RESOURCE_MBA,
	RDT_NUM_RESOURCES,
};

struct rdt_cbm {
	u64	id;
	u64	val;
};

struct rdt_schemata {
	int		 cnt;
	struct rdt_cbm	*cbm;
};

struct rdt_cache {
	u64	 cbm_mask;
	u64	 min_cbm_bits;
};

struct rdt_membw {
	u64	 bandwidth_gran;
	u64	 delay_linear;
	u64	 min_bandwidth;
};

struct rdt_resource {
	const char		*name;
	bool			 enabled;
	u64			 num_closids;
	struct rdt_cache	 cache;
	struct rdt_membw	 membw;
};

struct rdt_group {
	int			 id;
	const char		*name;
	struct rdt_schemata	 schemata[RDT_NUM_RESOURCES];
	struct cpu_map		*cpus;
	struct list_head	 list;
};

struct rdt_data {
	struct list_head	 groups;
	struct rdt_resource	 resource[RDT_NUM_RESOURCES];
};

int rdt_dump(FILE *file);
int rdt_display(FILE *file, struct rdt_data *rdt, bool hash);
int rdt_parse(struct rdt_data *data, char *file);
int rdt_parse_map(struct rdt_data *data, char *map);
struct rdt_group *rdt_group__find(struct perf_session *session, u32 closid);

#endif /* __PERF_RDT_H */
