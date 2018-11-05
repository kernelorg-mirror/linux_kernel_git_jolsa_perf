/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ORDERED_EVENTS_H
#define __ORDERED_EVENTS_H

#include <linux/types.h>
#include "queued-events.h"

struct perf_sample;

struct ordered_event {
	struct queued_event	qevent;
	u64			timestamp;
};

enum oe_flush {
	OE_FLUSH__NONE,
	OE_FLUSH__FINAL,
	OE_FLUSH__ROUND,
	OE_FLUSH__HALF,
};

struct ordered_events {
	struct queued_events		 qe;
	u64				 last_flush;
	u64				 next_flush;
	u64				 max_timestamp;
	struct ordered_event		*last;
	enum oe_flush			 last_flush_type;
	u32				 nr_unordered_events;
};

int ordered_events__queue(struct ordered_events *oe, union perf_event *event,
			  u64 timestamp, u64 file_offset);
int ordered_events__flush(struct ordered_events *oe, enum oe_flush how);
void ordered_events__init(struct ordered_events *oe, queued_events__deliver_t deliver);
void ordered_events__free(struct ordered_events *oe);
void ordered_events__reinit(struct ordered_events *oe);
#endif /* __ORDERED_EVENTS_H */
