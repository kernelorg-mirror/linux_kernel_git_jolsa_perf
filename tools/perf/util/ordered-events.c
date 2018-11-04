// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <inttypes.h>
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/string.h>
#include "ordered-events.h"
#include "session.h"
#include "asm/bug.h"
#include "debug.h"

#define pr_N(n, fmt, ...) \
	eprintf(n, debug_ordered_events, fmt, ##__VA_ARGS__)

#define pr(fmt, ...) pr_N(1, pr_fmt(fmt), ##__VA_ARGS__)

static void queue_event(struct ordered_events *oe, struct ordered_event *new)
{
	struct queued_events *qe = &oe->qe;
	struct queued_event *qnew = &new->qevent;
	struct ordered_event *last = oe->last;
	u64 timestamp = new->timestamp;
	struct list_head *p;

	++qe->nr_events;
	oe->last = new;

	pr_oe_time2(timestamp, "queue_event nr_events %u\n", qe->nr_events);

	if (!last) {
		list_add(&qnew->list, &qe->events);
		oe->max_timestamp = timestamp;
		return;
	}

	/*
	 * last event might point to some random place in the list as it's
	 * the last queued event. We expect that the new event is close to
	 * this.
	 */
	if (last->timestamp <= timestamp) {
		while (last->timestamp <= timestamp) {
			p = last->qevent.list.next;
			if (p == &qe->events) {
				list_add_tail(&qnew->list, &qe->events);
				oe->max_timestamp = timestamp;
				return;
			}
			last = list_entry(p, struct ordered_event, qevent.list);
		}
		list_add_tail(&qnew->list, &last->qevent.list);
	} else {
		while (last->timestamp > timestamp) {
			p = last->qevent.list.prev;
			if (p == &qe->events) {
				list_add(&qnew->list, &qe->events);
				return;
			}
			last = list_entry(p, struct ordered_event, qevent.list);
		}
		list_add(&qnew->list, &last->qevent.list);
	}
}

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

static struct queued_event *alloc_event(struct queued_events *qe,
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

static struct ordered_event *
alloc_ordered_event(struct ordered_events *oe, union perf_event *event)
{
	struct queued_event *qevent = alloc_event(&oe->qe, event);

	return qevent ? container_of(qevent, struct ordered_event, qevent) : NULL;
}

static struct ordered_event *
ordered_events__new_event(struct ordered_events *oe, u64 timestamp,
		    union perf_event *event)
{
	struct ordered_event *new;

	new = alloc_ordered_event(oe, event);
	if (new) {
		new->timestamp = timestamp;
		queue_event(oe, new);
	}

	return new;
}

int queued_events__queue(struct queued_events *qe, union perf_event *event,
			 u64 file_offset)
{
	struct queued_event *new;

	new = alloc_event(qe, event);
	if (new) {
		list_add(&new->list, &qe->events);
		new->file_offset = file_offset;
		qe->nr_events++;
	}

	return 0;
}

void ordered_events__delete(struct ordered_events *oe, struct ordered_event *event)
{
	list_move(&event->qevent.list, &oe->qe.cache);
	oe->qe.nr_events--;
	free_dup_event(&oe->qe, event->qevent.event);
	event->qevent.event = NULL;
}

int ordered_events__queue(struct ordered_events *oe, union perf_event *event,
			  u64 timestamp, u64 file_offset)
{
	struct ordered_event *oevent;

	if (!timestamp || timestamp == ~0ULL)
		return -ETIME;

	if (timestamp < oe->last_flush) {
		pr_oe_time(timestamp,      "out of order event\n");
		pr_oe_time(oe->last_flush, "last flush, last_flush_type %d\n",
			   oe->last_flush_type);

		oe->nr_unordered_events++;
	}

	oevent = ordered_events__new_event(oe, timestamp, event);
	if (!oevent) {
		ordered_events__flush(oe, OE_FLUSH__HALF);
		oevent = ordered_events__new_event(oe, timestamp, event);
	}

	if (!oevent)
		return -ENOMEM;

	oevent->qevent.file_offset = file_offset;
	return 0;
}

static int __ordered_events__flush(struct ordered_events *oe)
{
	struct list_head *head = &oe->qe.events;
	struct ordered_event *tmp, *iter;
	u64 limit = oe->next_flush;
	u64 last_ts = oe->last ? oe->last->timestamp : 0ULL;
	bool show_progress = limit == ULLONG_MAX;
	struct ui_progress prog;
	int ret;

	if (!limit)
		return 0;

	if (show_progress)
		ui_progress__init(&prog, oe->qe.nr_events, "Processing time ordered events...");

	list_for_each_entry_safe(iter, tmp, head, qevent.list) {
		if (session_done())
			return 0;

		if (iter->timestamp > limit)
			break;
		ret = oe->qe.deliver(&oe->qe, &iter->qevent);
		if (ret)
			return ret;

		ordered_events__delete(oe, iter);
		oe->last_flush = iter->timestamp;

		if (show_progress)
			ui_progress__update(&prog, 1);
	}

	if (list_empty(head))
		oe->last = NULL;
	else if (last_ts <= limit)
		oe->last = list_entry(head->prev, struct ordered_event, qevent.list);

	if (show_progress)
		ui_progress__finish();

	return 0;
}

int ordered_events__flush(struct ordered_events *oe, enum oe_flush how)
{
	static const char * const str[] = {
		"NONE",
		"FINAL",
		"ROUND",
		"HALF ",
	};
	int err;

	if (oe->qe.nr_events == 0)
		return 0;

	switch (how) {
	case OE_FLUSH__FINAL:
		oe->next_flush = ULLONG_MAX;
		break;

	case OE_FLUSH__HALF:
	{
		struct ordered_event *first, *last;
		struct list_head *head = &oe->qe.events;

		first = list_entry(head->next, struct ordered_event, qevent.list);
		last = oe->last;

		/* Warn if we are called before any event got allocated. */
		if (WARN_ONCE(!last || list_empty(head), "empty queue"))
			return 0;

		oe->next_flush  = first->timestamp;
		oe->next_flush += (last->timestamp - first->timestamp) / 2;
		break;
	}

	case OE_FLUSH__ROUND:
	case OE_FLUSH__NONE:
	default:
		break;
	};

	pr_oe_time(oe->next_flush, "next_flush - ordered_events__flush PRE  %s, nr_events %u\n",
		   str[how], oe->qe.nr_events);
	pr_oe_time(oe->max_timestamp, "max_timestamp\n");

	err = __ordered_events__flush(oe);

	if (!err) {
		if (how == OE_FLUSH__ROUND)
			oe->next_flush = oe->max_timestamp;

		oe->last_flush_type = how;
	}

	pr_oe_time(oe->next_flush, "next_flush - ordered_events__flush POST %s, nr_events %u\n",
		   str[how], oe->qe.nr_events);
	pr_oe_time(oe->last_flush, "last_flush\n");

	return err;
}

void queued_events__init(struct queued_events *qe, queued_events__deliver_t deliver,
			 unsigned int priv_size)
{
	INIT_LIST_HEAD(&qe->events);
	INIT_LIST_HEAD(&qe->cache);
	INIT_LIST_HEAD(&qe->to_free);
	qe->max_alloc_size = (u64) -1;
	qe->cur_alloc_size = 0;
	qe->deliver	   = deliver;
	qe->priv_size	   = priv_size ?: sizeof(struct queued_event);
	qe->buffer_max	   = 64 * 1024 / qe->priv_size;
}

void ordered_events__init(struct ordered_events *oe, queued_events__deliver_t deliver)
{
	queued_events__init(&oe->qe, deliver, sizeof(struct ordered_event));
}

static void
ordered_events_buffer__free(struct ordered_events_buffer *buffer,
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
	struct ordered_events_buffer *buffer, *tmp;

	if (list_empty(&qe->to_free))
		return;

	/*
	 * Current buffer might not have all the events allocated
	 * yet, we need to free only allocated ones ...
	 */
	list_del(&qe->buffer->list);
	ordered_events_buffer__free(qe->buffer, qe->buffer_idx, qe);

	/* ... and continue with the rest */
	list_for_each_entry_safe(buffer, tmp, &qe->to_free, list) {
		list_del(&buffer->list);
		ordered_events_buffer__free(buffer, qe->buffer_max, qe);
	}
}

void ordered_events__free(struct ordered_events *oe)
{
	queued_events__free(&oe->qe);
}

void ordered_events__reinit(struct ordered_events *oe)
{
	queued_events__deliver_t old_deliver = oe->qe.deliver;

	ordered_events__free(oe);
	memset(oe, '\0', sizeof(*oe));
	ordered_events__init(oe, old_deliver);
}
