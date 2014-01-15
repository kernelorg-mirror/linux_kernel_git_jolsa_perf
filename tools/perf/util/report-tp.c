#include <traceevent/event-parse.h>
#include "trace-event.h"
#include "evlist.h"
#include "evsel.h"
#include "sort.h"
#include "report-tp.h"

struct field_sort_entry {
	struct format_field	*field;
	struct sort_entry	 se;
};

struct format_sort_entry {
	struct event_format	*format;
	struct sort_entry	 se;
};

int report_tp_parse_mode(const struct option *opt,
			 const char *str, int unset)
{
	int *tp_mode = opt->value;

	if (unset) {
		*tp_mode = REPORT_TO_MODE__NONE;
		return 0;
	}

	if (!str || !strcmp(str, "fields")) {
		*tp_mode = REPORT_TO_MODE__FIELDS;
	} else if (!strcmp(str, "format"))
		*tp_mode = REPORT_TO_MODE__FORMAT;

	return 0;
}

static int64_t field_sort_entry__cmp(struct sort_entry *se,
				     struct hist_entry *left,
				     struct hist_entry *right)
{
	struct field_sort_entry *field_se;
	struct raw_info *raw_left = left->raw_info;
	struct raw_info *raw_right = right->raw_info;

	field_se = container_of(se, struct field_sort_entry, se);
	return pevent_field_cmp(field_se->field, field_se->field,
				raw_left->data, raw_left->size,
				raw_right->data, raw_right->size);
}

static int field_sort_entry__snprintf(struct sort_entry *se,
				   bool selected __maybe_unused,
				   struct hist_entry *he,
				   char *bf, size_t size,
				   unsigned int width)
{
	struct field_sort_entry *field_se;
	struct raw_info *raw = he->raw_info;
	struct trace_seq s;
	int n;

	trace_seq_init(&s);

	field_se = container_of(se, struct field_sort_entry, se);

	pevent_field_info(&s, field_se->field, raw->data, raw->size, false);
	n = scnprintf(bf, size, "%*s", width, s.buffer);
	trace_seq_destroy(&s);
	return n;
}

static struct field_sort_entry*
tp_sort_entry__new(struct format_field *field, int width_idx)
{
	struct field_sort_entry* field_se = zalloc(sizeof(*field_se));

	if (field_se) {
		INIT_LIST_HEAD(&field_se->se.list);
		field_se->se.se_header		= strdup(field->name);
		field_se->se.se_cmp		= field_sort_entry__cmp;
		field_se->se.se_snprintf	= field_sort_entry__snprintf;
		field_se->se.se_width_idx	= width_idx;
		field_se->field			= field;
	}

	return field_se;
}

int perf_format_field__width(struct format_field *field)
{
	int len = field->size * 2 + 2 /* '0x' */;

	if (field->flags & FIELD_IS_ARRAY)
		len = 30;

	return len;
}

static int add_tp_field_entries(struct perf_evsel *evsel)
{
	struct format_field **fields, **iter;
	struct hists *hists = &evsel->hists;
	struct field_sort_entry *field_se;
	int width, width_idx = HISTC_NR_COLS;
	int ret = -1;

	fields = iter = pevent_event_fields(evsel->tp_format);
	if (!fields)
		return 0;

	while (*iter) {
		field_se = tp_sort_entry__new(*iter, width_idx);
		if (!field_se)
			goto out;

		if (hists__alloc_col_len(hists, width_idx + 1))
			goto out;

		width = perf_format_field__width(*iter);
		hists__set_col_len(hists, width_idx, width);

		hists__sort_entry_add(hists, &field_se->se);

		width_idx++;
		iter++;
	}

	ret = 0;

 out:
	free(fields);
	return ret;
}

static int64_t format_sort_entry__cmp(struct sort_entry *se,
				      struct hist_entry *left,
				      struct hist_entry *right)
{
	struct format_sort_entry *format_se;
	struct raw_info *raw_left = left->raw_info;
	struct raw_info *raw_right = right->raw_info;
	struct format_field **fields, **iter;
	int ret = 0;

	format_se = container_of(se, struct format_sort_entry, se);

	fields = iter = pevent_event_fields(format_se->format);
	if (!fields)
		return 0;

	while (*iter) {
		struct format_field *field = *iter;

		ret = pevent_field_cmp(field, field,
				       raw_left->data, raw_left->size,
				       raw_right->data, raw_right->size);
		if (ret)
			break;

		iter++;
	}

	free(fields);
	return ret;
}

static int format_sort_entry__snprintf(struct sort_entry *se,
				       bool selected __maybe_unused,
				       struct hist_entry *he,
				       char *bf, size_t size,
				       unsigned int width)
{
	struct format_sort_entry *format_se;
	struct raw_info *raw = he->raw_info;
	struct pevent_record record;
	struct trace_seq s;
	int n;

	trace_seq_init(&s);

	memset(&record, 0, sizeof(record));
	record.cpu  = he->cpu;
	record.size = raw->size;
	record.data = raw->data;

	format_se = container_of(se, struct format_sort_entry, se);

	pevent_event_info(&s, format_se->format, &record);
	n = scnprintf(bf, size, "%*s", width, s.buffer);
	trace_seq_destroy(&s);
	return n;
}

static int add_tp_format_entry(struct perf_evsel *evsel)
{
	struct format_sort_entry* format_se = zalloc(sizeof(*format_se));
	struct hists *hists = &evsel->hists;
	int width_idx = HISTC_NR_COLS;

	if (!format_se)
		return -ENOMEM;

	INIT_LIST_HEAD(&format_se->se.list);
	format_se->se.se_header 	= "Print fmt";
	format_se->se.se_cmp		= format_sort_entry__cmp;
	format_se->se.se_snprintf	= format_sort_entry__snprintf;
	format_se->se.se_width_idx	= width_idx;
	format_se->format		= evsel->tp_format;

	if (hists__alloc_col_len(hists, width_idx + 1)) {
		free(format_se);
		return -ENOMEM;
	}

	hists__set_col_len(hists, width_idx, 9);
	hists__sort_entry_add(hists, &format_se->se);
	return 0;
}

static bool perf_evsel__is_tracepoint(struct perf_evsel *evsel)
{
	return evsel->attr.type == PERF_TYPE_TRACEPOINT;
}

int perf_evlist__add_tp_sort_entries(struct perf_evlist *evlist, enum report_tp_mode mode)
{
	struct perf_evsel *evsel;
	int ret = 0;

	list_for_each_entry(evsel, &evlist->entries, node) {
		if (!perf_evsel__is_tracepoint(evsel))
			continue;

		switch (mode) {
		case REPORT_TO_MODE__FIELDS:
			ret = add_tp_field_entries(evsel);
			break;
		case REPORT_TO_MODE__FORMAT:
			ret = add_tp_format_entry(evsel);
			break;
		case REPORT_TO_MODE__NONE:
		default:
			BUG_ON(1);
		}

		if (ret)
			break;
	}

	return ret;
}
