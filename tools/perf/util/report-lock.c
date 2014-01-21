#include <stdbool.h>
#include <linux/compiler.h>
#include <traceevent/event-parse.h>
#include "trace-event.h"
#include "report-lock.h"
#include "evlist.h"
#include "evsel.h"
#include "event.h"
#include "hist.h"
#include "parse-options.h"
#include "sort.h"
#include "session.h"

enum {
	LOCK_FIELD__LOCK_DEP_ADDR,
	LOCK_FIELD__NAME,
	LOCK_FIELD__MAX,
};

enum lock_tp {
	LOCK_TP__ACQUIRE,
	LOCK_TP__ACQUIRED,
	LOCK_TP__CONTENDED,
	LOCK_TP__RELEASE,
	LOCK_TP__MAX,
};

struct lock_event_info {
	enum lock_mode		mode;
	enum lock_tp		tp;
	struct format_field	*fields[LOCK_FIELD__MAX];
	struct format_field	*field_flags;
};

struct lock_info {
	struct lock_event_info *ev_info;
	int	lock_indent;
	int	lock_flag;
};

const struct hist_iter_ops hist_iter_lock;

static struct hists lock_hists;

u64 lock_hists_nr_samples(void)
{
	return lock_hists.stats.nr_events[PERF_RECORD_SAMPLE];
}

int hist_lock_iter_cb(struct hist_entry_iter *iter,
		     struct addr_location *al __maybe_unused,
		     bool single __maybe_unused,
		     void *arg __maybe_unused)
{
	struct perf_sample *sample = iter->sample;

	lock_hists.stats.total_period += sample->period;
	hists__inc_nr_events(&lock_hists, PERF_RECORD_SAMPLE);
	return 0;
}

int parse_lock_mode(const struct option *opt,
		    const char *str, int unset)
{
	int *lock_mode = opt->value;

	if (unset) {
		*lock_mode = LOCK__NONE;
		return 0;
	}

	if (!str || !strcmp(str, "cnt")) {
		*lock_mode = LOCK__CNT;
	} else if (!strcmp(str, "list"))
		*lock_mode = LOCK__LIST;

	return 0;
}

static const char *lock_tp_names[LOCK_TP__MAX] = {
	[LOCK_TP__ACQUIRE]	= "[a] ",
	[LOCK_TP__ACQUIRED]	= "[A] ",
	[LOCK_TP__CONTENDED]	= "[c] ",
	[LOCK_TP__RELEASE]	= "[r] ",
};

struct lock_sort_entry {
	unsigned idx;
	struct sort_entry se;
};

static int lock_indent(struct hist_entry *he, struct lock_event_info *info)
{
	int indent = 0;
	struct dso *dso = NULL;

	if (he->ms.map)
		dso = he->ms.map->dso;

	if (!dso)
		return 0;

	indent = dso->lock_indent;

	if (info->tp == LOCK_TP__ACQUIRE)
		dso->lock_indent++;
	if ((info->tp == LOCK_TP__RELEASE) &&
	    (dso->lock_indent > 0)) {
		dso->lock_indent--;
	}

	return indent < 0 ? 0 : indent;
}

static struct lock_info* alloc_lock_info(void)
{
	return zalloc(sizeof(struct lock_info));
}

static int
iter_add_single_lock_entry(struct hist_entry_iter *iter,
			   struct addr_location *al)
{
	struct perf_evsel *evsel = iter->evsel;
	struct perf_sample *sample = iter->sample;
	struct lock_event_info *ev_info = evsel->handler;
	struct lock_info *info;
	struct hist_entry *he;
	int flag = perf_evsel__intval(evsel, sample, "flag");

	info = alloc_lock_info();
	if (!info)
		return -ENOMEM;

	info->ev_info = ev_info;

	he = __hists__add_entry(&lock_hists, al, iter->parent,
				NULL, NULL, iter->raw, info,
				sample->period, sample->weight,
				sample->transaction, sample->time, true);
	if (he == NULL) {
		free(info);
		return -ENOMEM;
	}

	if (ev_info->mode == LOCK__LIST) {
		he->lock_info->lock_indent = lock_indent(he, info->ev_info);
		he->lock_info->lock_flag   = flag;
	}

	iter->he = he;
	return 0;
}

static int64_t lock_sort_entry__cmp(struct sort_entry *se,
				    struct hist_entry *left,
				    struct hist_entry *right)
{
	struct lock_sort_entry *lse = container_of(se, struct lock_sort_entry, se);

	struct raw_info *raw_left  = left->raw_info;
	struct raw_info *raw_right = right->raw_info;

	struct lock_event_info *info_left  = left->lock_info->ev_info;
	struct lock_event_info *info_right = right->lock_info->ev_info;

	struct format_field **fields_left  = (struct format_field **) &info_left->fields;
	struct format_field **fields_right = (struct format_field **) &info_right->fields;

	struct format_field *field_left  = fields_left[lse->idx];
	struct format_field *field_right = fields_right[lse->idx];

	return pevent_field_cmp2(field_left, field_right,
				raw_left->data,  raw_left->size,
				raw_right->data, raw_right->size);
}

static const char *lock_op(enum lock_tp tp)
{
	return lock_tp_names[tp];
}

enum acquire_flags {
	TRY_LOCK = 1,
	READ_LOCK = 2,
};

static int lock_sort_entry__snprintf(struct sort_entry *se,
				     bool selected __maybe_unused,
				     struct hist_entry *he,
				     char *bf, size_t size,
				     unsigned int width)
{
	struct lock_sort_entry *lse = container_of(se, struct lock_sort_entry, se);
	struct lock_event_info *info = he->lock_info->ev_info;
	struct format_field **fields = (struct format_field **) &info->fields;
	struct format_field *field   = fields[lse->idx];
	struct raw_info *raw = he->raw_info;
	static struct trace_seq s;
	int indent = 0;
	const char *op = "";
	const char *flags_str = " ";
	const char *pair = "";

	if (!s.len)
		trace_seq_init(&s);
	else
		trace_seq_reset(&s);

	if ((info->mode == LOCK__LIST) && (lse->idx == LOCK_FIELD__NAME)) {
		op     = lock_op(info->tp);
		indent = he->lock_info->lock_indent;
	}

	if (lse->idx == LOCK_FIELD__LOCK_DEP_ADDR) {
		struct hists *hists = he->hists;
		unsigned long long addr;

		pair = " ";
		pevent_read_number_field(field, raw->data, &addr);
		if (selected)
			hists->lockdep_addr_base = addr;
		else if (hists->lockdep_addr_base &&
			 hists->lockdep_addr_base == addr)
			pair = ">";
	}

	if (info->field_flags && (lse->idx == LOCK_FIELD__NAME)) {
		unsigned long long flags;

		flags_str = "L";
		pevent_read_number_field(info->field_flags, raw->data, &flags);

		if (flags & TRY_LOCK)
			flags_str = "T";
		if (flags & READ_LOCK)
			flags_str = "R";
	}

	pevent_field_info(&s, field, raw->data, raw->size, false);
	return scnprintf(bf, size, "%s%s %s%*s%-*s", pair, flags_str, op, indent * 2, "", width, s.buffer);
}

static struct lock_sort_entry lock_field__lockdep_addr = {
	.idx	= LOCK_FIELD__LOCK_DEP_ADDR,
	.se	= {
		.se_header	= "Lockdep_addr",
		.se_cmp		= lock_sort_entry__cmp,
		.se_snprintf	= lock_sort_entry__snprintf,
	},
};

static struct lock_sort_entry lock_field__se_name = {
	.idx	= LOCK_FIELD__NAME,
	.se	= {
		.se_header	= "Name",
		.se_cmp		= lock_sort_entry__cmp,
		.se_snprintf	= lock_sort_entry__snprintf,
	},
};

static const char *lock_common_fields[LOCK_FIELD__MAX] = {
	"lockdep_addr",
	"name",
};

static int lock_field_add(struct lock_sort_entry *lse, int width_idx, int len)
{
	if (hists__alloc_col_len(&lock_hists, width_idx + 1))
		return -1;

	lse->se.se_width_idx = width_idx;
	hists__set_col_len(&lock_hists, width_idx, len);
	hists__sort_entry_add(&lock_hists, &lse->se);
	return 0;
}

static int setup_lock_fields(struct perf_evlist *evlist, enum lock_mode mode)
{
	struct perf_evsel *evsel;
	struct tp {
		const char		*name;
		enum lock_tp		 idx;
		struct lock_event_info	 info;
	};
	static struct tp tps[] = {
		{
			.name = "lock:lock_acquire",
			.info = {
				.tp	= LOCK_TP__ACQUIRE,
			},
		},
		{
			.name = "lock:lock_acquired",
			.info = {
				.tp	= LOCK_TP__ACQUIRED,
			},
		},
		{
			.name = "lock:lock_contended",
			.info = {
				.tp	= LOCK_TP__CONTENDED,
			},
		},
		{
			.name = "lock:lock_release",
			.info = {
				.tp	= LOCK_TP__RELEASE,
			},
		},
	};
	int len_lockdep_addr = 0, len_name = 0;
	unsigned i, j;

	for (i = 0; i < ARRAY_SIZE(tps); i++) {
		struct tp *t = &tps[i];

		evsel = perf_evlist__find_tracepoint_by_name(evlist, t->name);
		if (!evsel) {
			pr_err("Could not find %s tracepoint.\n", t->name);
			return -1;
		}

		for (j = 0; j < LOCK_FIELD__MAX; j++) {
			const char *fname = lock_common_fields[j];
			struct format_field *field;

			field = pevent_find_field(evsel->tp_format, fname);
			if (!field) {
				pr_err("Could not find %s field.\n", fname);
				return -1;
			}

			if (!len_lockdep_addr && j == LOCK_FIELD__LOCK_DEP_ADDR)
				len_lockdep_addr = perf_format_field__width(field);

			if (!len_name && j == LOCK_FIELD__NAME)
				len_name = perf_format_field__width(field);

			t->info.fields[j] = field;
		}

		if (i == LOCK_TP__ACQUIRE) {
			struct format_field *field;

			field = pevent_find_field(evsel->tp_format, "flags");
			if (!field) {
				pr_err("Could not find flags field.\n");
				return -1;
			}
			t->info.field_flags = field;
		}

		t->info.mode = mode;
		evsel->handler = &t->info;
	}

	if (lock_field_add(&lock_field__lockdep_addr, HISTC_NR_COLS, len_lockdep_addr) ||
	    lock_field_add(&lock_field__se_name, HISTC_NR_COLS + 1, len_name)) {
		pr_err("Couldn't register lock sort entry\n");
		return -1;
	}

	if (mode == LOCK__LIST)
		sort__setup_idx();

	return 0;
}

int perf_lock__setup(struct perf_evlist *evlist, enum lock_mode mode)
{
	struct hist_iter_ops* ops = (struct hist_iter_ops*) &hist_iter_lock;

	if (hists__init(&lock_hists))
		return -1;

	*ops = hist_iter_normal;
	ops->add_single_entry = iter_add_single_lock_entry;

	return setup_lock_fields(evlist, mode);
}

int report__browse_lock_hists(void)
{
	int ret;

	hists__output_resort(&lock_hists);

	switch (use_browser) {
	case 1:
	case 2:
		ret = lock__hists_browse(&lock_hists);
		break;
	default:
		ret = hists__fprintf(&lock_hists, true, 0, 0, 0, stdout);
		fprintf(stdout, "\n\n");
		break;
	}

	return ret > 0 ? 0 : -1;
}
