#include <linux/list.h>
#include "ordered-events.h"
#include "evlist.h"
#include "session.h"
#include "asm/bug.h"

static void queue_event(struct ordered_events_queue *q, struct ordered_event *new)
{
	struct ordered_event *last = q->last;
	u64 timestamp = new->timestamp;
	struct list_head *p;

	++q->nr_events;
	q->last = new;

	if (!last) {
		list_add(&new->list, &q->events);
		q->max_timestamp = timestamp;
		return;
	}

	/*
	 * last event might point to some random place in the list as it's
	 * the last queued event. We expect that the new event is clqe to
	 * this.
	 */
	if (last->timestamp <= timestamp) {
		while (last->timestamp <= timestamp) {
			p = last->list.next;
			if (p == &q->events) {
				list_add_tail(&new->list, &q->events);
				q->max_timestamp = timestamp;
				return;
			}
			last = list_entry(p, struct ordered_event, list);
		}
		list_add_tail(&new->list, &last->list);
	} else {
		while (last->timestamp > timestamp) {
			p = last->list.prev;
			if (p == &q->events) {
				list_add(&new->list, &q->events);
				return;
			}
			last = list_entry(p, struct ordered_event, list);
		}
		list_add(&new->list, &last->list);
	}
}

#define MAX_SAMPLE_BUFFER	(64 * 1024 / sizeof(struct ordered_event))
static struct ordered_event* alloc_event(struct ordered_events_queue *q)
{
	struct list_head *cache = &q->cache;
	struct ordered_event *new;

	if (!list_empty(cache)) {
		new = list_entry(cache->next, struct ordered_event, list);
		list_del(&new->list);
	} else if (q->buffer) {
		new = q->buffer + q->buffer_idx;
		if (++q->buffer_idx == MAX_SAMPLE_BUFFER)
			q->buffer = NULL;
	} else {
		size_t size = MAX_SAMPLE_BUFFER * sizeof(*new);

		q->buffer = malloc(size);
		if (!q->buffer)
			return NULL;

		q->cur_alloc_size += size;
		list_add(&q->buffer->list, &q->to_free);
		q->buffer_idx = 2;
		new = q->buffer + 1;
	}

	return new;
}

struct ordered_event*
ordered_events_get(struct ordered_events_queue *q, u64 timestamp)
{
	struct ordered_event *new;

	new = alloc_event(q);
	if (new) {
		new->timestamp = timestamp;
		queue_event(q, new);
	}

	return new;
}

void
ordered_event_put(struct ordered_events_queue *q, struct ordered_event *iter)
{
	list_del(&iter->list);
	list_add(&iter->list, &q->cache);
	q->nr_events--;
}

static int __ordered_events_flush(struct perf_session *s,
				  struct perf_tool *tool)
{
	struct ordered_events_queue *q = &s->ordered_events;
	struct list_head *head = &q->events;
	struct ordered_event *tmp, *iter;
	struct perf_sample sample;
	u64 limit = q->next_flush;
	u64 last_ts = q->last ? q->last->timestamp : 0ULL;
	bool show_progress = limit == ULLONG_MAX;
	struct ui_progress prog;
	int ret;

	if (!tool->ordered_events || !limit)
		return 0;

	if (show_progress)
		ui_progress__init(&prog, q->nr_events, "Processing time ordered events...");

	list_for_each_entry_safe(iter, tmp, head, list) {
		if (session_done())
			return 0;

		if (iter->timestamp > limit)
			break;

		ret = perf_evlist__parse_sample(s->evlist, iter->event, &sample);
		if (ret)
			pr_err("Can't parse sample, err = %d\n", ret);
		else {
			ret = perf_session_deliver_event(s, iter->event, &sample, tool,
							 iter->file_offset);
			if (ret)
				return ret;
		}

		ordered_event_put(q, iter);
		q->last_flush = iter->timestamp;

		if (show_progress)
			ui_progress__update(&prog, 1);
	}

	if (list_empty(head))
		q->last = NULL;
	else if (last_ts <= limit)
		q->last = list_entry(head->prev, struct ordered_event, list);

	return 0;
}

int ordered_events_flush(struct perf_session *s, struct perf_tool *tool,
			 enum oeq_flush how)
{
	struct ordered_events_queue *q = &s->ordered_events;
	int err;

	switch (how) {
	case OEQ_FLUSH__FINAL:
		q->next_flush = ULLONG_MAX;
		break;

	case OEQ_FLUSH__HALF:
	{
		struct ordered_event *first, *last;
		struct list_head *head = &q->events;

		first = list_entry(head->next, struct ordered_event, list);
		last = q->last;

		if (WARN_ONCE(!last || list_empty(head), "empty queue"))
			return 0;

		q->next_flush  = first->timestamp;
		q->next_flush += (last->timestamp - first->timestamp) / 2;
		break;
	}

	case OEQ_FLUSH__ROUND:
	default:
		break;
	};

	err = __ordered_events_flush(s, tool);

	if (!err) {
		if (how == OEQ_FLUSH__ROUND)
			q->next_flush = q->max_timestamp;
	}

	return err;
}

void ordered_events_queue_init(struct ordered_events_queue *q)
{
	INIT_LIST_HEAD(&q->events);
	INIT_LIST_HEAD(&q->cache);
	INIT_LIST_HEAD(&q->to_free);
	q->max_alloc_size = (u64) -1;
	q->cur_alloc_size = 0;
}
