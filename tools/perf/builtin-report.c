/*
 * builtin-report.c
 *
 * Builtin report command: Analyze the perf.data input file,
 * look up and read DSOs and symbol information and display
 * a histogram of results, along various sorting keys.
 */
#include "builtin.h"

#include "util/util.h"
#include "util/cache.h"

#include "util/annotate.h"
#include "util/color.h"
#include <linux/list.h>
#include <linux/rbtree.h>
#include "util/symbol.h"
#include "util/callchain.h"
#include "util/strlist.h"
#include "util/values.h"

#include "perf.h"
#include "util/debug.h"
#include "util/evlist.h"
#include "util/evsel.h"
#include "util/header.h"
#include "util/session.h"
#include "util/tool.h"

#include "util/parse-options.h"
#include "util/parse-events.h"

#include "util/thread.h"
#include "util/sort.h"
#include "util/hist.h"
#include "util/data.h"
#include "arch/common.h"

#include <dlfcn.h>
#include <linux/bitmap.h>

static struct hists lock_hists;
static struct hist_iter_ops hist_iter_lock;

struct report {
	struct perf_tool	tool;
	struct perf_session	*session;
	bool			force, use_tui, use_gtk, use_stdio;
	bool			hide_unresolved;
	bool			dont_use_callchains;
	bool			show_full_info;
	bool			show_threads;
	bool			show_tp_entries;
	bool			show_lock_entries;
	bool			inverted_callchain;
	bool			mem_mode;
	bool			header;
	bool			header_only;
	int			max_stack;
	struct perf_read_values	show_threads_values;
	const char		*pretty_printing_style;
	const char		*cpu_list;
	const char		*symbol_filter_str;
	float			min_percent;
	DECLARE_BITMAP(cpu_bitmap, MAX_NR_CPUS);
};

static int report__config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "report.group")) {
		symbol_conf.event_group = perf_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "report.percent-limit")) {
		struct report *rep = cb;
		rep->min_percent = strtof(value, NULL);
		return 0;
	}
	if (!strcmp(var, "report.percentage")) {
		if (!strcmp(value, "relative"))
			symbol_conf.filter_relative = true;
		else if (!strcmp(value, "absolute"))
			symbol_conf.filter_relative = false;
		else
			return -1;
	}
	if (!strcmp(var, "report.children")) {
		symbol_conf.cumulate_callchain = perf_config_bool(var, value);
		return 0;
	}

	return perf_default_config(var, value, cb);
}

static int hist_lock_iter_cb(struct hist_entry_iter *iter,
			     struct addr_location *al __maybe_unused,
			     bool single __maybe_unused,
			     void *arg __maybe_unused)
{
	struct perf_sample *sample = iter->sample;

	lock_hists.stats.total_period += sample->period;
	hists__inc_nr_events(&lock_hists, PERF_RECORD_SAMPLE);
	return 0;
}

static int hist_iter_cb(struct hist_entry_iter *iter,
			struct addr_location *al, bool single,
			void *arg __maybe_unused)
{
	int err;
	struct hist_entry *he = iter->he;
	struct perf_evsel *evsel = iter->evsel;
	struct perf_sample *sample = iter->sample;

	err = hist_entry__inc_addr_samples(he, evsel->idx, al->addr);

	if (!single)
		goto out;

	evsel->hists.stats.total_period += sample->period;
	hists__inc_nr_events(&evsel->hists, PERF_RECORD_SAMPLE);

out:
	return err;
}

static int get_raw_info(struct raw_info **rawp, struct perf_sample *sample)
{
	struct raw_info *raw;

	if (!sample->raw_data)
		return 0;

	raw = malloc(sizeof(*raw) + sample->raw_size);
	if (raw) {
		memcpy(&raw->data, sample->raw_data, sample->raw_size);
		raw->size = sample->raw_size;
	}

	*rawp = raw;
	return raw ? 0 : -ENOMEM;
}

static int process_sample_event(struct perf_tool *tool,
				union perf_event *event,
				struct perf_sample *sample,
				struct perf_evsel *evsel,
				struct machine *machine)
{
	struct report *rep = container_of(tool, struct report, tool);
	struct addr_location al;
	struct hist_entry_iter iter = {
		.hide_unresolved = rep->hide_unresolved,
	};
	struct raw_info *raw = NULL;
	int ret;

	if (perf_event__preprocess_sample(event, machine, &al, sample) < 0) {
		pr_debug("problem processing %d event, skipping it.\n",
			 event->header.type);
		return -1;
	}

	if (rep->hide_unresolved && al.sym == NULL)
		return 0;

	if (rep->cpu_list && !test_bit(sample->cpu, rep->cpu_bitmap))
		return 0;

	if (rep->show_tp_entries || rep->show_lock_entries) {
		ret = get_raw_info(&raw, sample);
		if (ret)
			return ret;
	}

	if (sort__mode == SORT_MODE__BRANCH)
		iter.ops = &hist_iter_branch;
	else if (rep->mem_mode == 1)
		iter.ops = &hist_iter_mem;
	else if (symbol_conf.cumulate_callchain) {
		iter.ops = &hist_iter_cumulative;
		iter.add_entry_cb = hist_iter_cb;
	} else if (rep->show_lock_entries) {
		iter.ops = &hist_iter_lock;
		iter.add_entry_cb = hist_lock_iter_cb;
	} else {
		iter.ops = &hist_iter_normal;
		iter.add_entry_cb = hist_iter_cb;
	}

	iter.raw = raw;

	if (al.map != NULL)
		al.map->dso->hit = 1;

	ret = hist_entry_iter__add(&iter, &al, evsel, event, sample,
				   rep->max_stack, NULL);
	if (ret < 0) {
		pr_debug("problem adding hist entry, skipping event\n");
		free(raw);
	}

	return ret;
}

static int process_read_event(struct perf_tool *tool,
			      union perf_event *event,
			      struct perf_sample *sample __maybe_unused,
			      struct perf_evsel *evsel,
			      struct machine *machine __maybe_unused)
{
	struct report *rep = container_of(tool, struct report, tool);

	if (rep->show_threads) {
		const char *name = evsel ? perf_evsel__name(evsel) : "unknown";
		perf_read_values_add_value(&rep->show_threads_values,
					   event->read.pid, event->read.tid,
					   event->read.id,
					   name,
					   event->read.value);
	}

	dump_printf(": %d %d %s %" PRIu64 "\n", event->read.pid, event->read.tid,
		    evsel ? perf_evsel__name(evsel) : "FAIL",
		    event->read.value);

	return 0;
}

/* For pipe mode, sample_type is not currently set */
static int report__setup_sample_type(struct report *rep)
{
	struct perf_session *session = rep->session;
	u64 sample_type = perf_evlist__combined_sample_type(session->evlist);
	bool is_pipe = perf_data_file__is_pipe(session->file);

	if (!is_pipe && !(sample_type & PERF_SAMPLE_CALLCHAIN)) {
		if (sort__has_parent) {
			ui__error("Selected --sort parent, but no "
				    "callchain data. Did you call "
				    "'perf record' without -g?\n");
			return -EINVAL;
		}
		if (symbol_conf.use_callchain) {
			ui__error("Selected -g but no callchain data. Did "
				    "you call 'perf record' without -g?\n");
			return -1;
		}
	} else if (!rep->dont_use_callchains &&
		   callchain_param.mode != CHAIN_NONE &&
		   !symbol_conf.use_callchain) {
			symbol_conf.use_callchain = true;
			if (callchain_register_param(&callchain_param) < 0) {
				ui__error("Can't register callchain params.\n");
				return -EINVAL;
			}
	}

	if (symbol_conf.cumulate_callchain) {
		/* Silently ignore if callchain is missing */
		if (!(sample_type & PERF_SAMPLE_CALLCHAIN)) {
			symbol_conf.cumulate_callchain = false;
			perf_hpp__cancel_cumulate();
		}
	}

	if (sort__mode == SORT_MODE__BRANCH) {
		if (!is_pipe &&
		    !(sample_type & PERF_SAMPLE_BRANCH_STACK)) {
			ui__error("Selected -b but no branch data. "
				  "Did you call perf record without -b?\n");
			return -1;
		}
	}

	return 0;
}

static void sig_handler(int sig __maybe_unused)
{
	session_done = 1;
}

static size_t hists__fprintf_nr_sample_events(struct hists *hists, struct report *rep,
					      const char *evname, FILE *fp)
{
	size_t ret;
	char unit;
	unsigned long nr_samples = hists->stats.nr_events[PERF_RECORD_SAMPLE];
	u64 nr_events = hists->stats.total_period;
	struct perf_evsel *evsel = hists_to_evsel(hists);
	char buf[512];
	size_t size = sizeof(buf);

	if (perf_evsel__is_group_event(evsel)) {
		struct perf_evsel *pos;

		perf_evsel__group_desc(evsel, buf, size);
		evname = buf;

		for_each_group_member(pos, evsel) {
			nr_samples += pos->hists.stats.nr_events[PERF_RECORD_SAMPLE];
			nr_events += pos->hists.stats.total_period;
		}
	}

	nr_samples = convert_unit(nr_samples, &unit);
	ret = fprintf(fp, "# Samples: %lu%c", nr_samples, unit);
	if (evname != NULL)
		ret += fprintf(fp, " of event '%s'", evname);

	if (rep->mem_mode) {
		ret += fprintf(fp, "\n# Total weight : %" PRIu64, nr_events);
		ret += fprintf(fp, "\n# Sort order   : %s", sort_order);
	} else
		ret += fprintf(fp, "\n# Event count (approx.): %" PRIu64, nr_events);
	return ret + fprintf(fp, "\n#\n");
}

static int perf_evlist__tty_browse_hists(struct perf_evlist *evlist,
					 struct report *rep,
					 const char *help)
{
	struct perf_evsel *pos;

	evlist__for_each(evlist, pos) {
		struct hists *hists = &pos->hists;
		const char *evname = perf_evsel__name(pos);

		if (symbol_conf.event_group &&
		    !perf_evsel__is_group_leader(pos))
			continue;

		hists__fprintf_nr_sample_events(hists, rep, evname, stdout);
		hists__fprintf(hists, true, 0, 0, rep->min_percent, stdout);
		fprintf(stdout, "\n\n");
	}

	if (sort_order == default_sort_order &&
	    parent_pattern == default_parent_pattern) {
		fprintf(stdout, "#\n# (%s)\n#\n", help);

		if (rep->show_threads) {
			bool style = !strcmp(rep->pretty_printing_style, "raw");
			perf_read_values_display(stdout, &rep->show_threads_values,
						 style);
			perf_read_values_destroy(&rep->show_threads_values);
		}
	}

	return 0;
}

static void report__warn_kptr_restrict(const struct report *rep)
{
	struct map *kernel_map = rep->session->machines.host.vmlinux_maps[MAP__FUNCTION];
	struct kmap *kernel_kmap = map__kmap(kernel_map);

	if (kernel_map == NULL ||
	    (kernel_map->dso->hit &&
	     (kernel_kmap->ref_reloc_sym == NULL ||
	      kernel_kmap->ref_reloc_sym->addr == 0))) {
		const char *desc =
		    "As no suitable kallsyms nor vmlinux was found, kernel samples\n"
		    "can't be resolved.";

		if (kernel_map) {
			const struct dso *kdso = kernel_map->dso;
			if (!RB_EMPTY_ROOT(&kdso->symbols[MAP__FUNCTION])) {
				desc = "If some relocation was applied (e.g. "
				       "kexec) symbols may be misresolved.";
			}
		}

		ui__warning(
"Kernel address maps (/proc/{kallsyms,modules}) were restricted.\n\n"
"Check /proc/sys/kernel/kptr_restrict before running 'perf record'.\n\n%s\n\n"
"Samples in kernel modules can't be resolved as well.\n\n",
		desc);
	}
}

static int report__gtk_browse_hists(struct report *rep, const char *help)
{
	int (*hist_browser)(struct perf_evlist *evlist, const char *help,
			    struct hist_browser_timer *timer, float min_pcnt);

	hist_browser = dlsym(perf_gtk_handle, "perf_evlist__gtk_browse_hists");

	if (hist_browser == NULL) {
		ui__error("GTK browser not found!\n");
		return -1;
	}

	return hist_browser(rep->session->evlist, help, NULL, rep->min_percent);
}

static int report__browse_lock_hists(struct report *rep)
{
	struct perf_session *session = rep->session;
	int ret;

	switch (use_browser) {
	case 1:
	case 2:
		ret = hists__browse_tui(&lock_hists, 1, "krava", false, NULL, 0,
					&session->header.env);
		break;
	default:
		ret = hists__fprintf(&lock_hists, true, 0, 0, rep->min_percent, stdout);
		fprintf(stdout, "\n\n");
		break;
	}

	return ret > 0 ? 0 : -1;
}

static int report__browse_hists(struct report *rep)
{
	int ret;
	struct perf_session *session = rep->session;
	struct perf_evlist *evlist = session->evlist;
	const char *help = "For a higher level overview, try: perf report --sort comm,dso";

	switch (use_browser) {
	case 1:
		ret = perf_evlist__tui_browse_hists(evlist, help, NULL,
						    rep->min_percent,
						    &session->header.env);
		/*
		 * Usually "ret" is the last pressed key, and we only
		 * care if the key notifies us to switch data file.
		 */
		if (ret != K_SWITCH_INPUT_DATA)
			ret = 0;
		break;
	case 2:
		ret = report__gtk_browse_hists(rep, help);
		break;
	default:
		ret = perf_evlist__tty_browse_hists(evlist, rep, help);
		break;
	}

	return ret;
}

static u64 report__collapse_hists(struct report *rep)
{
	struct ui_progress prog;
	struct perf_evsel *pos;
	u64 nr_samples = 0;
	/*
 	 * Count number of histogram entries to use when showing progress,
 	 * reusing nr_samples variable.
 	 */
	evlist__for_each(rep->session->evlist, pos)
		nr_samples += pos->hists.nr_entries;

	ui_progress__init(&prog, nr_samples, "Merging related events...");
	/*
	 * Count total number of samples, will be used to check if this
 	 * session had any.
 	 */
	nr_samples = 0;

	evlist__for_each(rep->session->evlist, pos) {
		struct hists *hists = &pos->hists;

		if (pos->idx == 0)
			hists->symbol_filter_str = rep->symbol_filter_str;

		hists__collapse_resort(hists, &prog);
		nr_samples += hists->stats.nr_events[PERF_RECORD_SAMPLE];

		/* Non-group events are considered as leader */
		if (symbol_conf.event_group &&
		    !perf_evsel__is_group_leader(pos)) {
			struct hists *leader_hists = &pos->leader->hists;

			hists__match(leader_hists, hists);
			hists__link(leader_hists, hists);
		}
	}

	ui_progress__finish();

	return nr_samples;
}

static int __cmd_report(struct report *rep)
{
	int ret;
	u64 nr_samples;
	struct perf_session *session = rep->session;
	struct perf_evsel *pos;
	struct perf_data_file *file = session->file;

	signal(SIGINT, sig_handler);

	if (rep->cpu_list) {
		ret = perf_session__cpu_bitmap(session, rep->cpu_list,
					       rep->cpu_bitmap);
		if (ret)
			return ret;
	}

	if (rep->show_threads)
		perf_read_values_init(&rep->show_threads_values);

	ret = report__setup_sample_type(rep);
	if (ret)
		return ret;

	ret = perf_session__process_events(session, &rep->tool);
	if (ret)
		return ret;

	report__warn_kptr_restrict(rep);

	if (use_browser == 0) {
		if (verbose > 3)
			perf_session__fprintf(session, stdout);

		if (verbose > 2)
			perf_session__fprintf_dsos(session, stdout);

		if (dump_trace) {
			perf_session__fprintf_nr_events(session, stdout);
			return 0;
		}
	}

	if (rep->show_lock_entries)
		nr_samples = lock_hists.stats.nr_events[PERF_RECORD_SAMPLE];
	else
		nr_samples = report__collapse_hists(rep);

	if (session_done())
		return 0;

	if (nr_samples == 0) {
		ui__error("The %s file has no samples!\n", file->path);
		return 0;
	}

	if (rep->show_lock_entries) {
		hists__output_resort(&lock_hists);
		return report__browse_lock_hists(rep);
	}

	evlist__for_each(session->evlist, pos)
		hists__output_resort(&pos->hists);

	return report__browse_hists(rep);
}

static int
parse_callchain_opt(const struct option *opt, const char *arg, int unset)
{
	struct report *rep = (struct report *)opt->value;
	char *tok, *tok2;
	char *endptr;

	/*
	 * --no-call-graph
	 */
	if (unset) {
		rep->dont_use_callchains = true;
		return 0;
	}

	symbol_conf.use_callchain = true;

	if (!arg)
		return 0;

	tok = strtok((char *)arg, ",");
	if (!tok)
		return -1;

	/* get the output mode */
	if (!strncmp(tok, "graph", strlen(arg)))
		callchain_param.mode = CHAIN_GRAPH_ABS;

	else if (!strncmp(tok, "flat", strlen(arg)))
		callchain_param.mode = CHAIN_FLAT;

	else if (!strncmp(tok, "fractal", strlen(arg)))
		callchain_param.mode = CHAIN_GRAPH_REL;

	else if (!strncmp(tok, "none", strlen(arg))) {
		callchain_param.mode = CHAIN_NONE;
		symbol_conf.use_callchain = false;

		return 0;
	}

	else
		return -1;

	/* get the min percentage */
	tok = strtok(NULL, ",");
	if (!tok)
		goto setup;

	callchain_param.min_percent = strtod(tok, &endptr);
	if (tok == endptr)
		return -1;

	/* get the print limit */
	tok2 = strtok(NULL, ",");
	if (!tok2)
		goto setup;

	if (tok2[0] != 'c') {
		callchain_param.print_limit = strtoul(tok2, &endptr, 0);
		tok2 = strtok(NULL, ",");
		if (!tok2)
			goto setup;
	}

	/* get the call chain order */
	if (!strncmp(tok2, "caller", strlen("caller")))
		callchain_param.order = ORDER_CALLER;
	else if (!strncmp(tok2, "callee", strlen("callee")))
		callchain_param.order = ORDER_CALLEE;
	else
		return -1;

	/* Get the sort key */
	tok2 = strtok(NULL, ",");
	if (!tok2)
		goto setup;
	if (!strncmp(tok2, "function", strlen("function")))
		callchain_param.key = CCKEY_FUNCTION;
	else if (!strncmp(tok2, "address", strlen("address")))
		callchain_param.key = CCKEY_ADDRESS;
	else
		return -1;
setup:
	if (callchain_register_param(&callchain_param) < 0) {
		pr_err("Can't register callchain params\n");
		return -1;
	}
	return 0;
}

int
report_parse_ignore_callees_opt(const struct option *opt __maybe_unused,
				const char *arg, int unset __maybe_unused)
{
	if (arg) {
		int err = regcomp(&ignore_callees_regex, arg, REG_EXTENDED);
		if (err) {
			char buf[BUFSIZ];
			regerror(err, &ignore_callees_regex, buf, sizeof(buf));
			pr_err("Invalid --ignore-callees regex: %s\n%s", arg, buf);
			return -1;
		}
		have_ignore_callees = 1;
	}

	return 0;
}

static int
parse_branch_mode(const struct option *opt __maybe_unused,
		  const char *str __maybe_unused, int unset)
{
	int *branch_mode = opt->value;

	*branch_mode = !unset;
	return 0;
}

static int
parse_percent_limit(const struct option *opt, const char *str,
		    int unset __maybe_unused)
{
	struct report *rep = opt->value;

	rep->min_percent = strtof(str, NULL);
	return 0;
}

static int
parse_percentage(const struct option *opt __maybe_unused, const char *str,
		 int unset __maybe_unused)
{
	if (!strcmp(str, "relative"))
		symbol_conf.filter_relative = true;
	else if (!strcmp(str, "absolute"))
		symbol_conf.filter_relative = false;
	else
		return -1;

	return 0;
}

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

static int tp_col_width(struct format_field *field)
{
	int len = field->size * 2 + 2 /* '0x' */;

	if (field->flags & FIELD_IS_ARRAY)
		len = 50;

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
		fse = tp_sort_entry__new(*iter, width_idx);
		if (!fse)
			goto out;

		if (hists__alloc_col_len(hists, width_idx + 1))
			goto out;

		hists__set_col_len(hists, width_idx, tp_col_width(*iter));
		hists__sort_entry_add(hists, &fse->se);
		width_idx++;
		iter++;
	}

	ret = 0;

 out:
	free(fields);
	return ret;
}

static int perf_evlist__add_tp_sort_entries(struct perf_evlist *evlist)
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

static int
iter_add_single_lock_entry(struct hist_entry_iter *iter,
			   struct addr_location *al)
{
	struct perf_evsel *evsel = iter->evsel;
	struct perf_sample *sample = iter->sample;
	struct hist_entry *he;

	he = __hists__add_entry(&lock_hists, al, iter->parent,
				NULL, NULL, iter->raw, evsel->handler,
				sample->period, sample->weight,
				sample->transaction, 0, true);
	if (he == NULL)
		return -ENOMEM;

	iter->he = he;
	return 0;
}

enum {
	LOCK_FIELD__LOCK_DEP_ADDR,
	LOCK_FIELD__NAME,
	LOCK_FIELD__MAX,
};

struct lock_sort_entry {
	unsigned idx;
	struct sort_entry se;
};

static int64_t lock_sort_entry__cmp(struct sort_entry *se,
				    struct hist_entry *left,
				    struct hist_entry *right)
{
	struct lock_sort_entry *lse = container_of(se, struct lock_sort_entry, se);

	struct raw_info *raw_left  = left->raw_info;
	struct raw_info *raw_right = right->raw_info;

	struct format_field **fields_left  = left->lock_info;
	struct format_field **fields_right = right->lock_info;

	struct format_field *field_left  = fields_left[lse->idx];
	struct format_field *field_right = fields_right[lse->idx];

	return pevent_field_cmp2(field_left, field_right,
				raw_left->data,  raw_left->size,
				raw_right->data, raw_right->size);
}

static int lock_sort_entry__snprintf(struct sort_entry *se,
				     struct hist_entry *he,
				     char *bf, size_t size,
				     unsigned int width)
{
	struct lock_sort_entry *lse = container_of(se, struct lock_sort_entry, se);
	struct format_field **fields = he->lock_info;
	struct format_field *field   = fields[lse->idx];
	struct raw_info *raw = he->raw_info;
	static struct trace_seq s;

	if (!s.len)
		trace_seq_init(&s);
	else
		trace_seq_reset(&s);

	pevent_field_info(&s, field, raw->data, raw->size, false);
	return scnprintf(bf, size, "%*s", width, s.buffer);
}


static struct lock_sort_entry lock_field__lockdep_addr = {
	.idx	= LOCK_FIELD__LOCK_DEP_ADDR,
	.se	= {
		.se_header	= "lockdep_addr",
		.se_cmp		= lock_sort_entry__cmp,
		.se_snprintf	= lock_sort_entry__snprintf,
	},
};

static struct lock_sort_entry lock_field__se_name = {
	.idx	= LOCK_FIELD__NAME,
	.se	= {
		.se_header	= "name",
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

static int setup_lock_fields(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;
	struct tp {
		const char *name;
		struct format_field *fields[LOCK_FIELD__MAX];
	};
	static struct tp tps[] = {
		{ .name = "lock:lock_acquire",	},
		{ .name = "lock:lock_acquired",	},
		{ .name = "lock:lock_contended",},
		{ .name = "lock:lock_release",	},
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
				len_lockdep_addr = tp_col_width(field);

			if (!len_name && j == LOCK_FIELD__NAME)
				len_name = tp_col_width(field);

			t->fields[j] = field;
		}

		evsel->handler = &t->fields;
	}

	if (lock_field_add(&lock_field__lockdep_addr, HISTC_NR_COLS, len_lockdep_addr) ||
	    lock_field_add(&lock_field__se_name, HISTC_NR_COLS + 1, len_name)) {
		pr_err("Couldn't register lock sort entry\n");
		return -1;
	}

	return 0;
}

static int setup_lock(struct perf_session *session)
{
	if (hists__init(&lock_hists))
		return -1;

	hist_iter_lock = hist_iter_normal;
	hist_iter_lock.add_single_entry = iter_add_single_lock_entry;

	return setup_lock_fields(session->evlist);
}

int cmd_report(int argc, const char **argv, const char *prefix __maybe_unused)
{
	struct perf_session *session;
	struct stat st;
	bool has_br_stack = false;
	int branch_mode = -1;
	int ret = -1;
	char callchain_default_opt[] = "fractal,0.5,callee";
	const char * const report_usage[] = {
		"perf report [<options>]",
		NULL
	};
	struct report report = {
		.tool = {
			.sample		 = process_sample_event,
			.mmap		 = perf_event__process_mmap,
			.mmap2		 = perf_event__process_mmap2,
			.comm		 = perf_event__process_comm,
			.exit		 = perf_event__process_exit,
			.fork		 = perf_event__process_fork,
			.lost		 = perf_event__process_lost,
			.read		 = process_read_event,
			.attr		 = perf_event__process_attr,
			.tracing_data	 = perf_event__process_tracing_data,
			.build_id	 = perf_event__process_build_id,
			.ordered_samples = true,
			.ordering_requires_timestamps = true,
		},
		.max_stack		 = PERF_MAX_STACK_DEPTH,
		.pretty_printing_style	 = "normal",
	};
	const struct option options[] = {
	OPT_STRING('i', "input", &input_name, "file",
		    "input file name"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_STRING(0, "kallsyms", &symbol_conf.kallsyms_name,
		   "file", "kallsyms pathname"),
	OPT_BOOLEAN('f', "force", &report.force, "don't complain, do it"),
	OPT_BOOLEAN('m', "modules", &symbol_conf.use_modules,
		    "load module symbols - WARNING: use only with -k and LIVE kernel"),
	OPT_BOOLEAN('n', "show-nr-samples", &symbol_conf.show_nr_samples,
		    "Show a column with the number of samples"),
	OPT_BOOLEAN('T', "threads", &report.show_threads,
		    "Show per-thread event counters"),
	OPT_STRING(0, "pretty", &report.pretty_printing_style, "key",
		   "pretty printing style key: normal raw"),
	OPT_BOOLEAN(0, "tui", &report.use_tui, "Use the TUI interface"),
	OPT_BOOLEAN(0, "gtk", &report.use_gtk, "Use the GTK2 interface"),
	OPT_BOOLEAN(0, "stdio", &report.use_stdio,
		    "Use the stdio interface"),
	OPT_BOOLEAN(0, "header", &report.header, "Show data header."),
	OPT_BOOLEAN(0, "header-only", &report.header_only,
		    "Show only data header."),
	OPT_STRING('s', "sort", &sort_order, "key[,key2...]",
		   "sort by key(s): pid, comm, dso, symbol, parent, cpu, srcline,"
		   " dso_to, dso_from, symbol_to, symbol_from, mispredict,"
		   " weight, local_weight, mem, symbol_daddr, dso_daddr, tlb, "
		   "snoop, locked, abort, in_tx, transaction"),
	OPT_BOOLEAN(0, "showcpuutilization", &symbol_conf.show_cpu_utilization,
		    "Show sample percentage for different cpu modes"),
	OPT_STRING('p', "parent", &parent_pattern, "regex",
		   "regex filter to identify parent, see: '--sort parent'"),
	OPT_BOOLEAN('x', "exclude-other", &symbol_conf.exclude_other,
		    "Only display entries with parent-match"),
	OPT_CALLBACK_DEFAULT('g', "call-graph", &report, "output_type,min_percent[,print_limit],call_order",
		     "Display callchains using output_type (graph, flat, fractal, or none) , min percent threshold, optional print limit, callchain order, key (function or address). "
		     "Default: fractal,0.5,callee,function", &parse_callchain_opt, callchain_default_opt),
	OPT_BOOLEAN(0, "children", &symbol_conf.cumulate_callchain,
		    "Accumulate callchains of children and show total overhead as well"),
	OPT_INTEGER(0, "max-stack", &report.max_stack,
		    "Set the maximum stack depth when parsing the callchain, "
		    "anything beyond the specified depth will be ignored. "
		    "Default: " __stringify(PERF_MAX_STACK_DEPTH)),
	OPT_BOOLEAN('G', "inverted", &report.inverted_callchain,
		    "alias for inverted call graph"),
	OPT_CALLBACK(0, "ignore-callees", NULL, "regex",
		   "ignore callees of these functions in call graphs",
		   report_parse_ignore_callees_opt),
	OPT_STRING('d', "dsos", &symbol_conf.dso_list_str, "dso[,dso...]",
		   "only consider symbols in these dsos"),
	OPT_STRING('c', "comms", &symbol_conf.comm_list_str, "comm[,comm...]",
		   "only consider symbols in these comms"),
	OPT_STRING('S', "symbols", &symbol_conf.sym_list_str, "symbol[,symbol...]",
		   "only consider these symbols"),
	OPT_STRING(0, "symbol-filter", &report.symbol_filter_str, "filter",
		   "only show symbols that (partially) match with this filter"),
	OPT_STRING('w', "column-widths", &symbol_conf.col_width_list_str,
		   "width[,width...]",
		   "don't try to adjust column width, use these fixed values"),
	OPT_STRING('t', "field-separator", &symbol_conf.field_sep, "separator",
		   "separator for columns, no spaces will be added between "
		   "columns '.' is reserved."),
	OPT_BOOLEAN('U', "hide-unresolved", &report.hide_unresolved,
		    "Only display entries resolved to a symbol"),
	OPT_STRING(0, "symfs", &symbol_conf.symfs, "directory",
		    "Look for files with symbols relative to this directory"),
	OPT_STRING('C', "cpu", &report.cpu_list, "cpu",
		   "list of cpus to profile"),
	OPT_BOOLEAN('I', "show-info", &report.show_full_info,
		    "Display extended information about perf.data file"),
	OPT_BOOLEAN(0, "source", &symbol_conf.annotate_src,
		    "Interleave source code with assembly code (default)"),
	OPT_BOOLEAN(0, "asm-raw", &symbol_conf.annotate_asm_raw,
		    "display raw encoding of assembly instructions (default)"),
	OPT_STRING('M', "disassembler-style", &disassembler_style, "disassembler style",
		   "Specify disassembler style (e.g. -M intel for intel syntax)"),
	OPT_BOOLEAN(0, "show-total-period", &symbol_conf.show_total_period,
		    "Show a column with the sum of periods"),
	OPT_BOOLEAN(0, "group", &symbol_conf.event_group,
		    "Show event group information together"),
	OPT_CALLBACK_NOOPT('b', "branch-stack", &branch_mode, "",
		    "use branch records for histogram filling", parse_branch_mode),
	OPT_STRING(0, "objdump", &objdump_path, "path",
		   "objdump binary to use for disassembly and annotations"),
	OPT_BOOLEAN(0, "demangle", &symbol_conf.demangle,
		    "Disable symbol demangling"),
	OPT_BOOLEAN(0, "mem-mode", &report.mem_mode, "mem access profile"),
	OPT_CALLBACK(0, "percent-limit", &report, "percent",
		     "Don't show entries under that percent", parse_percent_limit),
	OPT_CALLBACK(0, "percentage", NULL, "relative|absolute",
		     "how to display percentage of filtered entries", parse_percentage),
	OPT_BOOLEAN(0, "list", &symbol_conf.show_list, "Show events list"),
	OPT_BOOLEAN(0, "tp", &report.show_tp_entries, "Show/sort tracepoints entries."),
	OPT_BOOLEAN(0, "lock", &report.show_lock_entries, "Show/sort lock entries."),
	OPT_END()
	};
	struct perf_data_file file = {
		.mode  = PERF_DATA_MODE_READ,
	};

	perf_config(report__config, &report);

	argc = parse_options(argc, argv, options, report_usage, 0);

	if (report.use_stdio)
		use_browser = 0;
	else if (report.use_tui)
		use_browser = 1;
	else if (report.use_gtk)
		use_browser = 2;

	if (report.inverted_callchain)
		callchain_param.order = ORDER_CALLER;

	if (!input_name || !strlen(input_name)) {
		if (!fstat(STDIN_FILENO, &st) && S_ISFIFO(st.st_mode))
			input_name = "-";
		else
			input_name = "perf.data";
	}

	file.path  = input_name;
	file.force = report.force;

repeat:
	session = perf_session__new(&file, false, &report.tool);
	if (session == NULL)
		return -ENOMEM;

	report.session = session;

	has_br_stack = perf_header__has_feat(&session->header,
					     HEADER_BRANCH_STACK);

	if (branch_mode == -1 && has_br_stack)
		sort__mode = SORT_MODE__BRANCH;

	/* sort__mode could be NORMAL if --no-branch-stack */
	if (sort__mode == SORT_MODE__BRANCH) {
		/*
		 * if no sort_order is provided, then specify
		 * branch-mode specific order
		 */
		if (sort_order == default_sort_order)
			sort_order = "comm,dso_from,symbol_from,"
				     "dso_to,symbol_to";

	}
	if (report.mem_mode) {
		if (sort__mode == SORT_MODE__BRANCH) {
			pr_err("branch and mem mode incompatible\n");
			goto error;
		}
		sort__mode = SORT_MODE__MEMORY;

		/*
		 * if no sort_order is provided, then specify
		 * branch-mode specific order
		 */
		if (sort_order == default_sort_order)
			sort_order = "local_weight,mem,sym,dso,symbol_daddr,dso_daddr,snoop,tlb,locked";
	}

	if (report.show_lock_entries) {
		sort_order = "pid";
		symbol_conf.cumulate_callchain = false;
	}

	if (setup_sorting() < 0) {
		parse_options_usage(report_usage, options, "s", 1);
		goto error;
	}

	if (parent_pattern != default_parent_pattern) {
		if (sort_dimension__add("parent") < 0)
			goto error;
	}

	/* Force tty output for header output. */
	if (report.header || report.header_only)
		use_browser = 0;

	if (strcmp(input_name, "-") != 0)
		setup_browser(true);
	else {
		use_browser = 0;
		perf_hpp__init();
	}

	if (report.header || report.header_only) {
		perf_session__fprintf_info(session, stdout,
					   report.show_full_info);
		if (report.header_only)
			return 0;
	} else if (use_browser == 0) {
		fputs("# To display the perf.data header info, please use --header/--header-only options.\n#\n",
		      stdout);
	}

	/*
	 * Only in the TUI browser we are doing integrated annotation,
	 * so don't allocate extra space that won't be used in the stdio
	 * implementation.
	 */
	if (use_browser == 1 && sort__has_sym) {
		symbol_conf.priv_size = sizeof(struct annotation);
		machines__set_symbol_filter(&session->machines,
					    symbol__annotate_init);
		/*
 		 * For searching by name on the "Browse map details".
 		 * providing it only in verbose mode not to bloat too
 		 * much struct symbol.
 		 */
		if (verbose) {
			/*
			 * XXX: Need to provide a less kludgy way to ask for
			 * more space per symbol, the u32 is for the index on
			 * the ui browser.
			 * See symbol__browser_index.
			 */
			symbol_conf.priv_size += sizeof(u32);
			symbol_conf.sort_by_name = true;
		}
	}

	if (symbol__init() < 0)
		goto error;

	if (argc) {
		/*
		 * Special case: if there's an argument left then assume that
		 * it's a symbol filter:
		 */
		if (argc > 1)
			usage_with_options(report_usage, options);

		report.symbol_filter_str = argv[0];
	}

	sort__setup_elide(stdout);

	if (symbol_conf.show_list)
		sort__setup_idx();

	if (report.show_tp_entries && report.show_lock_entries)
		report.show_tp_entries = false;

	if (report.show_tp_entries) {
		ret = perf_evlist__add_tp_sort_entries(session->evlist);
		if (ret) {
			pr_err("failed to add tracepoints sort entries\n");
			goto error;
		}
	}

	if (report.show_lock_entries) {
		ret = setup_lock(session);
		if (ret) {
			pr_err("failed to setup lock report\n");
			goto error;
		}
	}

	ret = __cmd_report(&report);
	if (ret == K_SWITCH_INPUT_DATA) {
		perf_session__delete(session);
		goto repeat;
	} else
		ret = 0;

error:
	perf_session__delete(session);
	return ret;
}
