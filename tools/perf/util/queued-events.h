/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __QUEUED_EVENTS_H
#define __QUEUED_EVENTS_H

#include <linux/types.h>

struct queued_event {
	u64			file_offset;
	union perf_event	*event;
	struct list_head	list;
};

struct queued_events;

typedef int (*queued_events__deliver_t)(struct queued_events *qe,
					struct queued_event *event);

struct queued_events_buffer {
	struct list_head	list;
	unsigned char		data[0];
};

struct queued_events {
	u64				 max_alloc_size;
	u64				 cur_alloc_size;
	struct list_head		 events;
	struct list_head		 cache;
	struct list_head		 to_free;
	struct queued_events_buffer	*buffer;
	queued_events__deliver_t	 deliver;
	int				 buffer_idx;
	int				 buffer_max;
	unsigned int			 nr_events;
	bool				 copy_on_queue;
	unsigned int			 priv_size;
};

static inline
void queued_events__set_alloc_size(struct queued_events *qe, u64 size)
{
	qe->max_alloc_size = size;
}

static inline
void queued_events__set_copy_on_queue(struct queued_events *qe, bool copy)
{
	qe->copy_on_queue = copy;
}

void queued_events__init(struct queued_events *qe, queued_events__deliver_t deliver,
			 unsigned int priv_size);
void queued_events__free(struct queued_events *qe);
int queued_events__queue(struct queued_events *qe, union perf_event *event,
			 u64 file_offset);
int queued_events__flush(struct queued_events *qe);
struct queued_event* queued_event__alloc(struct queued_events *qe, union perf_event *event);
void queued_event__delete(struct queued_events *qe, struct queued_event *event);
#endif /* __QUEUED_EVENTS_H */
