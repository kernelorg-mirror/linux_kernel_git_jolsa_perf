#ifndef __PERF_RDT_H
#define __PERF_RDT_H

#include <linux/list.h>
#include <linux/types.h>
#include <cpumap.h>
#include <thread_map.h>

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
	u32			 id;
	const char		*name;
	struct rdt_schemata	 schemata[RDT_NUM_RESOURCES];
	struct cpu_map		*cpus;
	struct thread_map	*threads;
	struct list_head	 list;
};

struct rdt_data {
	struct list_head	 groups;
	struct rdt_resource	 resource[RDT_NUM_RESOURCES];
};

int
perf_event__synthesize_rdt(struct perf_tool *tool,
			   perf_event__handler_t process,
			   const char *resctrlfs);
int perf_event__process_rdt(struct perf_tool *tool,
			    union perf_event *event,
			    struct perf_session *session);
struct rdt_group *rdt_group__find(struct rdt_data *data, u32 closid);
int rdt_load(struct rdt_data *data, const char *resctrl);
void rdt_data__init(struct rdt_data *data);
int rdt_dump(FILE *file);
int rdt_display(FILE *file, struct rdt_data *rdt, bool hash);

#endif /* __PERF_RDT_H */
