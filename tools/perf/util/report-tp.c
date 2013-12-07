#include <traceevent/event-parse.h>
#include "evlist.h"
#include "evsel.h"
#include "sort.h"

struct field_sort_entry {
	struct format_field *field;
	struct sort_entry se;
};

static bool perf_evsel__is_tracepoint(struct perf_evsel *evsel)
{
	return evsel->attr.type == PERF_TYPE_TRACEPOINT;
}

static int64_t tp_sort_entry__cmp(struct sort_entry *se,
				  struct hist_entry *left,
				  struct hist_entry *right)
{
	struct field_sort_entry *fse = container_of(se, struct field_sort_entry, se);
	struct raw_info *raw_left = left->raw_info;
	struct raw_info *raw_right = right->raw_info;

	return pevent_field_cmp(fse->field,
				raw_left->data,  raw_left->size,
				raw_right->data, raw_right->size);
}

static int tp_sort_entry__snprintf(struct sort_entry *se,
				   struct hist_entry *he,
				   char *bf, size_t size,
				   unsigned int width)
{
	struct field_sort_entry *fse = container_of(se, struct field_sort_entry, se);
	struct raw_info *raw = he->raw_info;
	static struct trace_seq s;

	if (!s.len)
		trace_seq_init(&s);
	else
		trace_seq_reset(&s);

	pevent_field_info(&s, fse->field, raw->data, raw->size, false);
	return scnprintf(bf, size, "%*s", width, s.buffer);
}

static struct field_sort_entry*
tp_sort_entry__new(struct format_field *field, int width_idx)
{
	struct field_sort_entry* fse = zalloc(sizeof(*fse));

	if (fse) {
		INIT_LIST_HEAD(&fse->se.list);
		fse->se.se_header 	= strdup(field->name);
		fse->se.se_cmp		= tp_sort_entry__cmp;
		fse->se.se_snprintf	= tp_sort_entry__snprintf;
		fse->se.se_width_idx	= width_idx;
		fse->field		= field;
	}

	return fse;
}

static int perf_format_field__width(struct format_field *field)
{
	int len = field->size * 2 + 2 /* '0x' */;

	if (field->flags & FIELD_IS_ARRAY)
		len = 30;

	return len;
}

static int perf_evsel__add_tp_sort_entries(struct perf_evsel *evsel)
{
	struct format_field **fields, **iter;
	struct hists *hists = &evsel->hists;
	struct field_sort_entry *fse;
	int width_idx = HISTC_NR_COLS;
	int ret = -1;

	fields = iter = pevent_event_fields(evsel->tp_format);
	if (!fields)
		return 0;

	while (*iter) {
		int width;

		fse = tp_sort_entry__new(*iter, width_idx);
		if (!fse)
			goto out;

		if (hists__alloc_col_len(hists, width_idx + 1))
			goto out;

		width = perf_format_field__width(*iter);
		hists__set_col_len(hists, width_idx, width);
		hists__sort_entry_add(hists, &fse->se);
		width_idx++;
		iter++;
	}

	ret = 0;

 out:
	free(fields);
	return ret;
}

int perf_evlist__add_tp_sort_entries(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;
	int ret = 0;

	list_for_each_entry(evsel, &evlist->entries, node) {
		if (!perf_evsel__is_tracepoint(evsel))
			continue;

		ret = perf_evsel__add_tp_sort_entries(evsel);
		if (ret)
			break;
	}

	return ret;
}
