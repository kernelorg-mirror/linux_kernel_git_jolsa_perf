// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <inttypes.h>
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/string.h>
#include "queued-events.h"
#include "session.h"
#include "asm/bug.h"
#include "debug.h"

#define pr_N(n, fmt, ...) \
	eprintf(n, debug_ordered_events, fmt, ##__VA_ARGS__)

#define pr(fmt, ...) pr_N(1, pr_fmt(fmt), ##__VA_ARGS__)

static union perf_event *__dup_event(struct queued_events *qe,
				     union perf_event *event)
{
	union perf_event *new_event = NULL;

	if (qe->cur_alloc_size < qe->max_alloc_size) {
		new_event = memdup(event, event->header.size);
		if (new_event)
			qe->cur_alloc_size += event->header.size;
	}

	return new_event;
}

static union perf_event *dup_event(struct queued_events *qe,
				   union perf_event *event)
{
	return qe->copy_on_queue ? __dup_event(qe, event) : event;
}

static void __free_dup_event(struct queued_events *qe, union perf_event *event)
{
	if (event) {
		qe->cur_alloc_size -= event->header.size;
		free(event);
	}
}

static void free_dup_event(struct queued_events *qe, union perf_event *event)
{
	if (qe->copy_on_queue)
		__free_dup_event(qe, event);
}

static struct queued_event *buffer_data(struct queued_events *qe, int idx)
{
	return (void *) &qe->buffer->data[0] + idx * qe->priv_size;
}

struct queued_event *queued_event__alloc(struct queued_events *qe,
					 union perf_event *event)
{
	struct list_head *cache = &qe->cache;
	struct queued_event *new = NULL;
	union perf_event *new_event;
	size_t size;

	new_event = dup_event(qe, event);
	if (!new_event)
		return NULL;

	/*
	 * We maintain the following scheme of buffers for ordered
	 * event allocation:
	 *
	 *   to_free list -> buffer1 (64K)
	 *                   buffer2 (64K)
	 *                   ...
	 *
	 * Each buffer keeps an array of ordered events objects:
	 *    buffer -> event[0]
	 *              event[1]
	 *              ...
	 *
	 * Each allocated ordered event is linked to one of
	 * following lists:
	 *   - time ordered list 'events'
	 *   - list of currently removed events 'cache'
	 *
	 * Allocation of the ordered event uses the following order
	 * to get the memory:
	 *   - use recently removed object from 'cache' list
	 *   - use available object in current allocation buffer
	 *   - allocate new buffer if the current buffer is full
	 *
	 * Removal of ordered event object moves it from events to
	 * the cache list.
	 */
	size = sizeof(*qe->buffer) + qe->buffer_max * qe->priv_size;

	if (!list_empty(cache)) {
		new = list_entry(cache->next, struct queued_event, list);
		list_del(&new->list);
	} else if (qe->buffer) {
		new = buffer_data(qe, qe->buffer_idx);
		if (++qe->buffer_idx == qe->buffer_max)
			qe->buffer = NULL;
	} else if ((qe->cur_alloc_size + size) < qe->max_alloc_size) {
		qe->buffer = malloc(size);
		if (!qe->buffer) {
			free_dup_event(qe, new_event);
			return NULL;
		}

		pr("alloc size %" PRIu64 "B (+%zu), max %" PRIu64 "B\n",
		   qe->cur_alloc_size, size, qe->max_alloc_size);

		qe->cur_alloc_size += size;
		list_add(&qe->buffer->list, &qe->to_free);

		qe->buffer_idx = 1;
		new = buffer_data(qe, 0);
	} else {
		pr("allocation limit reached %" PRIu64 "B\n", qe->max_alloc_size);
		return NULL;
	}

	new->event = new_event;
	return new;
}

int queued_events__queue(struct queued_events *qe, union perf_event *event,
			 u64 file_offset)
{
	struct queued_event *new;

	new = queued_event__alloc(qe, event);
	if (new) {
		list_add(&new->list, &qe->events);
		new->file_offset = file_offset;
		qe->nr_events++;
	}

	return 0;
}

void queued_event__delete(struct queued_events *qe, struct queued_event *event)
{
	list_move(&event->list, &qe->cache);
	qe->nr_events--;
	free_dup_event(qe, event->event);
	event->event = NULL;
}

int queued_events__flush(struct queued_events *qe)
{
	struct list_head *head = &qe->events;
	struct queued_event *tmp, *iter;
	int ret;

	list_for_each_entry_safe(iter, tmp, head, list) {
		if (session_done())
			return 0;

		ret = qe->deliver(qe, iter);
		if (ret)
			return ret;

		queued_event__delete(qe, iter);
	}

	return 0;
}

void queued_events__init(struct queued_events *qe, queued_events__deliver_t deliver,
			 unsigned int priv_size, void *data)
{
	INIT_LIST_HEAD(&qe->events);
	INIT_LIST_HEAD(&qe->cache);
	INIT_LIST_HEAD(&qe->to_free);
	qe->max_alloc_size = (u64) -1;
	qe->cur_alloc_size = 0;
	qe->deliver	   = deliver;
	qe->priv_size	   = priv_size ?: sizeof(struct queued_event);
	qe->buffer_max	   = 64 * 1024 / qe->priv_size;
	qe->data	   = data;
}

static void
queued_events_buffer__free(struct queued_events_buffer *buffer,
			   unsigned int max, struct queued_events *qe)
{
	if (qe->copy_on_queue) {
		unsigned int i;

		for (i = 0; i < max; i++)
			__free_dup_event(qe, buffer_data(qe, i)->event);
	}

	free(buffer);
}

void queued_events__free(struct queued_events *qe)
{
	struct queued_events_buffer *buffer, *tmp;

	if (list_empty(&qe->to_free))
		return;

	/*
	 * Current buffer might not have all the events allocated
	 * yet, we need to free only allocated ones ...
	 */
	list_del(&qe->buffer->list);
	queued_events_buffer__free(qe->buffer, qe->buffer_idx, qe);

	/* ... and continue with the rest */
	list_for_each_entry_safe(buffer, tmp, &qe->to_free, list) {
		list_del(&buffer->list);
		queued_events_buffer__free(buffer, qe->buffer_max, qe);
	}
}
