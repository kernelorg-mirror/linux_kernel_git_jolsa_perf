/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ORDERED_EVENTS_H
#define __ORDERED_EVENTS_H

#include <linux/types.h>

struct perf_sample;

struct queued_event {
	u64			file_offset;
	union perf_event	*event;
	struct list_head	list;
};

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

struct queued_events;

typedef int (*queued_events__deliver_t)(struct queued_events *qe,
					struct queued_event *event);

struct ordered_events_buffer {
	struct list_head	list;
	unsigned char		data[0];
};

struct queued_events {
	u64				 max_alloc_size;
	u64				 cur_alloc_size;
	struct list_head		 events;
	struct list_head		 cache;
	struct list_head		 to_free;
	struct ordered_events_buffer	*buffer;
	queued_events__deliver_t	 deliver;
	int				 buffer_idx;
	int				 buffer_max;
	unsigned int			 nr_events;
	bool				 copy_on_queue;
	unsigned int			 priv_size;
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
void ordered_events__delete(struct ordered_events *oe, struct ordered_event *event);
int ordered_events__flush(struct ordered_events *oe, enum oe_flush how);
void ordered_events__init(struct ordered_events *oe, queued_events__deliver_t deliver);
void ordered_events__free(struct ordered_events *oe);
void ordered_events__reinit(struct ordered_events *oe);

static inline
void ordered_events__set_alloc_size(struct ordered_events *oe, u64 size)
{
	oe->qe.max_alloc_size = size;
}

static inline
void ordered_events__set_copy_on_queue(struct ordered_events *oe, bool copy)
{
	oe->qe.copy_on_queue = copy;
}
#endif /* __ORDERED_EVENTS_H */
