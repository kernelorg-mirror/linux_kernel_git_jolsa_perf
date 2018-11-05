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

static struct ordered_event *
alloc_ordered_event(struct ordered_events *oe, union perf_event *event)
{
	struct queued_event *qevent = queued_event__alloc(&oe->qe, event);

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

		queued_event__delete(&oe->qe, &iter->qevent);
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

void ordered_events__init(struct ordered_events *oe, queued_events__deliver_t deliver)
{
	queued_events__init(&oe->qe, deliver, sizeof(struct ordered_event), NULL);
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
