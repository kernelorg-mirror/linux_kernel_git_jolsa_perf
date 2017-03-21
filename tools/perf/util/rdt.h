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
	RDT_NUM_RESOURCES,
};

struct rdt_cbm {
	u64	id;
	u64	val;
};

struct rdt_schemata {
	bool		 enabled;
	int		 cnt;
	struct rdt_cbm	*cbm;
};

struct rdt_resource {
	const char	*name;
	bool		 enabled;
	u64		 cbm_mask;
	u64		 min_cbm_bits;
	u64		 num_closids;
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

enum {
	RDT_CONFIG_TYPE__CBM_MASK,
	RDT_CONFIG_TYPE__MIN_CBM_BITS,
	RDT_CONFIG_TYPE__NUM_CLOSIDS,
	RDT_CONFIG_TYPE__IDS,
	RDT_CONFIG_TYPE__ID,
	RDT_CONFIG_TYPE__CPUS,
	RDT_CONFIG_TYPE__SCHEMATA,
	RDT_CONFIG_TYPE__SCHEMATA_LINE,
	RDT_CONFIG_TYPE__SCHEMATA_ASS,
	RDT_CONFIG_TYPE__GROUP_ID,
};

struct rdt_config {
	int type;
	union {
		u64 cbm_mask;
		u64 min_cbm_bits;
		u64 num_closids;
		int group_id;
		struct {
			int   val;
			char *map;
		} id;
		struct {
			struct list_head *head;
		} ids;
		struct {
			char *id;
			char *val;
		} schemata_ass;
		struct {
			char *name;
			struct list_head *head;
		} schemata_line;
		struct {
			struct list_head *head;
		} schemata;
		struct {
			char *map;
		} cpus;
	};
	struct list_head list;
};

int rdt_group__add(struct rdt_data *data, char *name, struct list_head *head);
int rdt_resource__add(struct rdt_data *data, char *name, struct list_head *head);
#endif /* __PERF_RDT_H */
