#ifndef __PERF_RDT_H
#define __PERF_RDT_H

enum {
	RDT_RESOURCE_L3,
	RDT_RESOURCE_L3DATA,
	RDT_RESOURCE_L3CODE,
	RDT_RESOURCE_L2,
	RDT_NUM_RESOURCES,
};

int
perf_event__synthesize_rdt(struct perf_tool *tool,
			   perf_event__handler_t process,
			   const char *resctrlfs);

#endif /* __PERF_RDT_H */
