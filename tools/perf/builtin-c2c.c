#include <linux/compiler.h>
#include <linux/kernel.h>
#include "util.h"
#include "debug.h"
#include "builtin.h"
#include <subcmd/parse-options.h>
#include "mem-events.h"
#include "session.h"
#include "hist.h"
#include "tool.h"
#include "data.h"
#include "sort.h"
#include "evlist.h"

struct perf_c2c {
	struct perf_tool tool;
	struct c2c_hists c2c_hists;
	bool		 dont_use_callchains;
	int		 max_stack;
	bool		 use_stdio;
};

static struct perf_c2c c2c;

#define C2C_HISTS (&c2c.c2c_hists.hists)

static int c2c_hists__init(struct c2c_hists *c2c_hists,
			   const char *sort, const char *output)
{
	__hists__init(&c2c_hists->hists);
	c2c_hists->hists.hpp_list = &c2c_hists->hpp_list;
	perf_hpp_list__init(&c2c_hists->hpp_list);
	return hists__setup_hpp_list(&c2c_hists->hists, sort, output);
}

static int c2c_hists__reinit(struct c2c_hists *c2c_hists,
			     const char *sort, const char *output)
{
	perf_hpp__reset_output_field(&c2c_hists->hpp_list);
	return hists__setup_hpp_list(&c2c_hists->hists, sort, output);
}

static struct c2c_hists* he__get_c2c_hists(struct hist_entry *he,
					   const char *sort, const char *output)
{
	struct c2c_hists *c2c_hists = he->c2c_hists;

	if (!c2c_hists) {
		he->c2c_hists = c2c_hists = malloc(sizeof(*c2c_hists));
		if (!c2c_hists)
			return NULL;

		c2c_hists__init(c2c_hists, sort, output);
	}

	return c2c_hists;
}

static struct hist_entry*
__he__add_offset_entry(struct hists *hists, struct hist_entry *entry,
		       struct perf_sample *sample __maybe_unused)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct hist_entry *he;
	int64_t cmp;

	p = &hists->entries_in->rb_node;

	while (*p != NULL) {
		parent = *p;
		he = rb_entry(parent, struct hist_entry, rb_node_in);

		cmp = hist_entry__cmp(he, entry);

		if (!cmp) {
			c2c_decode_stats(&he->c2c_stats, entry);

			/*
			 * This mem info was allocated from sample__resolve_mem
			 * and will not be used anymore.
			 */
			zfree(&entry->mem_info);

			/* If the map of an existing hist_entry has
			 * become out-of-date due to an exec() or
			 * similar, update it.  Otherwise we will
			 * mis-adjust symbol addresses when computing
			 * the history counter to increment.
			 */
			if (he->ms.map != entry->ms.map) {
				map__put(he->ms.map);
				he->ms.map = map__get(entry->ms.map);
			}

			goto out;
		}

		if (cmp < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	he = hist_entry__new(entry, true);
	if (!he)
		return NULL;

	hists->nr_entries++;

	rb_link_node(&he->rb_node_in, parent, p);
	rb_insert_color(&he->rb_node, hists->entries_in);
out:
	hist_entry__append_callchain(he, sample);
	return he;
}

static struct hist_entry*
he__add_offset_entry(struct hist_entry *he, struct hist_entry *entry,
		     struct perf_sample *sample)
{
	struct c2c_hists *c2c_hists;

	c2c_hists = he__get_c2c_hists(he, "overhead,symbol,pid,cpu,dso", NULL);
	if (!c2c_hists)
		return NULL;

	entry->hists = &c2c_hists->hists;

	/* Account cacheline offset overall stats. */
	c2c_decode_stats(&c2c_hists->hists.c2c_stats, entry);

	return __he__add_offset_entry(&c2c_hists->hists, entry, sample);
}

static struct hist_entry*
__he__add_cacheline_entry(struct hists *hists, struct hist_entry *entry,
			  struct perf_sample *sample)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct hist_entry *he;
	int64_t cmp;

	p = &hists->entries_in->rb_node;

	while (*p != NULL) {
		parent = *p;
		he = rb_entry(parent, struct hist_entry, rb_node_in);

		cmp = hist_entry__cmp(he, entry);

		if (!cmp) {
			/* If the map of an existing hist_entry has
			 * become out-of-date due to an exec() or
			 * similar, update it.  Otherwise we will
			 * mis-adjust symbol addresses when computing
			 * the history counter to increment.
			 */
			if (he->ms.map != entry->ms.map) {
				map__put(he->ms.map);
				he->ms.map = map__get(entry->ms.map);
			}

			c2c_decode_stats(&he->c2c_stats, entry);
			goto out;
		}

		if (cmp < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	he = hist_entry__new(entry, true);
	if (!he)
		return NULL;

	hists->nr_entries++;

	rb_link_node(&he->rb_node_in, parent, p);
	rb_insert_color(&he->rb_node, hists->entries_in);
out:
	hist_entry__append_callchain(he, sample);
	return he__add_offset_entry(he, entry, sample);
}

static struct hist_entry*
he__add_cacheline_entry(struct hist_entry *he, struct hist_entry *entry,
			struct perf_sample *sample)
{
	struct c2c_hists *c2c_hists;

	c2c_hists = he__get_c2c_hists(he, "c2c_offset", "symbol_daddr");
	if (!c2c_hists)
		return NULL;

	entry->hists = &c2c_hists->hists;

	/* Account cacheline offset overall stats. */
	c2c_decode_stats(&c2c_hists->hists.c2c_stats, entry);

	return __he__add_cacheline_entry(&c2c_hists->hists, entry, sample);
}

static struct hist_entry*
__hists__add_main_entry(struct hists *hists, struct hist_entry *entry,
			struct perf_sample *sample)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct hist_entry *he;
	int64_t cmp;

	p = &hists->entries_in->rb_node;

	while (*p != NULL) {
		parent = *p;
		he = rb_entry(parent, struct hist_entry, rb_node_in);

		cmp = hist_entry__cmp(he, entry);

		if (!cmp) {
			/* If the map of an existing hist_entry has
			 * become out-of-date due to an exec() or
			 * similar, update it.  Otherwise we will
			 * mis-adjust symbol addresses when computing
			 * the history counter to increment.
			 */
			if (he->ms.map != entry->ms.map) {
				map__put(he->ms.map);
				he->ms.map = map__get(entry->ms.map);
			}

			c2c_decode_stats(&he->c2c_stats, entry);
			goto out;
		}

		if (cmp < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	he = hist_entry__new(entry, true);
	if (!he)
		return NULL;

	hists->nr_entries++;

	rb_link_node(&he->rb_node_in, parent, p);
	rb_insert_color(&he->rb_node_in, hists->entries_in);
out:
	if (!c2c.use_stdio)
		hist_entry__append_callchain(he, sample);
	return he__add_cacheline_entry(he, entry, sample);
}

static struct hist_entry*
hists__add_main_entry(struct hists *hists, struct addr_location *al,
		     struct mem_info *mi, struct perf_sample *sample)
{
	struct hist_entry entry = {
		.thread	= al->thread,
		.comm = thread__comm(al->thread),
		.ms = {
			.map	= al->map,
			.sym	= al->sym,
		},
		.socket		= al->socket,
		.cpu		= al->cpu,
		.cpumode	= al->cpumode,
		.ip		= al->addr,
		.level		= al->level,
		.hists		= hists,
		.mem_info	= mi,
	};

	/* Account entry itself. */
	c2c_decode_stats(&entry.c2c_stats, &entry);

	/* Account overall numbers. */
	c2c_decode_stats(&hists->c2c_stats, &entry);

	return __hists__add_main_entry(hists, &entry, sample);
}

static int process_sample_event(struct perf_tool *tool __maybe_unused,
				union perf_event *event,
				struct perf_sample *sample,
				struct perf_evsel *evsel,
				struct machine *machine)
{
	struct addr_location al;
	struct mem_info *mi;
	struct hist_entry *he;

	if (perf_event__preprocess_sample(event, machine, &al, sample) < 0) {
		fprintf(stderr, "problem processing %d event, skipping it.\n",
				event->header.type);
		return -1;
	}

	if (sample__resolve_callchain(sample, NULL, evsel, &al, c2c.max_stack))
		return -1;

	mi = sample__resolve_mem(sample, &al);
	if (mi == NULL)
		return -ENOMEM;

	he = hists__add_main_entry(C2C_HISTS, &al, mi, sample);
	return he ? 0 : -1;
}

static struct perf_c2c c2c = {
	.tool = {
		.sample		= process_sample_event,
		.mmap		= perf_event__process_mmap,
		.mmap2		= perf_event__process_mmap2,
		.comm		= perf_event__process_comm,
		.lost		= perf_event__process_lost,
		.fork		= perf_event__process_fork,
		.build_id	= perf_event__process_build_id,
		.ordered_events	= true,
	},
	.max_stack = PERF_MAX_STACK_DEPTH,
};

static const char * const c2c_usage[] = {
	"perf c2c {record|report}",
	NULL
};

static const char * const __usage_report[] = {
	"perf c2c report",
	NULL
};

static const char * const *report_c2c_usage = __usage_report;

#define HAS_HITMS(_he) (_he->c2c_stats.t.lcl_hitm || _he->c2c_stats.t.rmt_hitm)

static int perf_c2c__stdio_browse(struct hists *hists)
{
        struct rb_node *nd;

	printf("\n#\n");
	printf("# Shared Data Cache Line Table\n");
	printf("# ============================\n");
	printf("#\n");

	hists__fprintf(hists, true, 0, 0, 0, stdout);

	printf("\nShared Cache Line Distribution Pareto\n\n");

        nd = rb_first(&hists->entries);
	do {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);
		struct c2c_hists *c2c_hists = he->c2c_hists;

		if (HAS_HITMS(he)) {
			printf("\n#\n");
			printf("# Cacheline: 0x%lx\n", he->mem_info->daddr.addr);
			printf("# ==========\n");
			printf("#\n");

			hists__fprintf(&c2c_hists->hists, true, 0, 0, 0, stdout);
		}

                nd = rb_next(nd);
        } while (nd);

	return 0;
}

static void resort_offset_cb(struct hist_entry *he)
{
	struct c2c_hists *c2c_hists = he->c2c_hists;

	if (c2c_hists)
		hists__output_resort(&c2c_hists->hists, NULL);
}

static void resort_cl_cb(struct hist_entry *he)
{
	struct c2c_hists *c2c_hists = he->c2c_hists;
	if (c2c_hists)
		hists__output_resort_cb(&c2c_hists->hists, NULL, resort_offset_cb);

	if (!HAS_HITMS(he))
		he->filtered = (1 << HIST_FILTER__C2C_HITM);
}

static int perf_c2c_report(void)
{
	c2c_hists__reinit(&c2c.c2c_hists, "c2c_dcacheline", NULL);
	hists__output_resort_cb(C2C_HISTS, NULL, resort_cl_cb);

	if (c2c.use_stdio)
		return perf_c2c__stdio_browse(C2C_HISTS);

	return perf_c2c__hists_browse(C2C_HISTS);
}

#define CALLCHAIN_DEFAULT_OPT  "graph,0.5,caller,function,percent"

const char c2c_callchain_help[] = "Display call graph (stack chain/backtrace):\n\n"
				     CALLCHAIN_REPORT_HELP
				     "\n\t\t\t\tDefault: " CALLCHAIN_DEFAULT_OPT;

static int
c2c_parse_callchain_opt(const struct option *opt __maybe_unused,
			const char *arg, int unset)
{
	/*
	 * --no-call-graph
	 */
	if (unset) {
		c2c.dont_use_callchains = true;
		return 0;
	}

	return parse_callchain_c2c_opt(arg);
}

static int setup_callchains(struct perf_session *session)
{
	u64 sample_type = perf_evlist__combined_sample_type(session->evlist);

	if (!(sample_type & PERF_SAMPLE_CALLCHAIN)) {
		if (symbol_conf.use_callchain) {
			ui__error("Selected -g or --branch-history but no "
				  "callchain data. Did\n"
				  "you call 'perf record' without -g?\n");
			return -1;
		}
	} else if (!c2c.dont_use_callchains &&
		   callchain_param.mode != CHAIN_NONE &&
		   !symbol_conf.use_callchain) {
		symbol_conf.use_callchain = true;
		if (callchain_register_param(&callchain_param) < 0) {
			ui__error("Can't register callchain params.\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int perf_c2c__report(int argc, const char **argv)
{
	struct perf_session *session;
	struct perf_data_file file = {
		.path = input_name,
		.mode = PERF_DATA_MODE_READ,
	};
	char callchain_default_opt[] = CALLCHAIN_DEFAULT_OPT;
	const struct option c2c_options[] = {
	OPT_INCR('v', "verbose", &verbose,
		 "be more verbose (show counter open errors, etc)"),
	OPT_STRING('i', "input", &input_name, "file",
		   "the input file to process"),
	OPT_BOOLEAN(0, "stdio", &c2c.use_stdio,
		    "Use the stdio interface"),
	OPT_CALLBACK_DEFAULT('g', "call-graph", NULL,
			     "print_type,threshold[,print_limit],order,sort_key[,branch],value",
			     c2c_callchain_help, &c2c_parse_callchain_opt,
			     callchain_default_opt),
	OPT_END()
	};
	int err = 0;

	argc = parse_options(argc, argv, c2c_options, report_c2c_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	/*
	 * The use_browser variable is -1 by default,
	 *  which will set TUI in setup_browser.
	 */
	if (c2c.use_stdio)
		use_browser = 0;

	setup_browser(true);

	if (c2c_hists__init(&c2c.c2c_hists, "c2c_dcacheline", NULL))
		return -1;

	session = perf_session__new(&file, 0, &c2c.tool);
	if (session == NULL) {
		pr_debug("No memory for session\n");
		goto out;
	}

	/* No pipe support at the moment. */
	if (perf_data_file__is_pipe(session->file)) {
		pr_debug("No pipe support at the moment.\n");
		goto out_session;
	}

	if (setup_callchains(session)) {
		pr_err("Failed to setup callchains.\n");
		goto out_session;
	}

	err = perf_session__process_events(session);
	if (!err)
		err = perf_c2c_report();

out_session:
	perf_session__delete(session);
out:
	return err;
}

static int parse_record_events(const struct option *opt __maybe_unused,
			       const char *str, int unset __maybe_unused)
{
	bool *event_set = (bool*) opt->value;
	int j;

	*event_set = true;

	if (strcmp(str, "list"))
		return perf_mem_events__parse(str);

	fprintf(stderr, "available events:\n");

	for (j = 0; j < PERF_MEM_EVENTS__MAX; j++) {
		struct perf_mem_event *e = &perf_mem_events[j];

		fprintf(stderr, "%s %s\n",
			e->supported ? "[ok] " : "[n/a]", e->name);
	}

	exit(0);
}


static const char * const __usage_record[] = {
	"perf c2c record [<options>] [<command>]",
	"perf c2c record [<options>] -- <command> [<options>]",
	NULL
};

static const char * const *record_mem_usage = __usage_record;

static int perf_c2c__record(int argc, const char **argv)
{
	int rec_argc, i = 0, j;
	const char **rec_argv;
	int ret;
	bool all_user = false, all_kernel = false;
	bool event_set = false;
	struct option options[] = {
	OPT_CALLBACK('e', "event", &event_set, "event",
		     "event selector. use 'perf mem record -e list' to list available events",
		     parse_record_events),
	OPT_INCR('v', "verbose", &verbose,
		 "be more verbose (show counter open errors, etc)"),
	OPT_BOOLEAN('u', "--all-user", &all_user, "collect only user level data"),
	OPT_BOOLEAN('k', "--all-kernel", &all_kernel, "collect only kernel level data"),
	OPT_END()
	};

	if (perf_mem_events__init()) {
		pr_err("failed: memory events not supported\n");
		return -1;
	}

	argc = parse_options(argc, argv, options, record_mem_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	rec_argc = argc + 9; /* max number of arguments */
	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	if (!rec_argv)
		return -1;

	rec_argv[i++] = "record";

	if (!event_set) {
		perf_mem_events[PERF_MEM_EVENTS__LOAD].record  = true;
		perf_mem_events[PERF_MEM_EVENTS__STORE].record = true;
		all_kernel = true;
	}

	if (perf_mem_events[PERF_MEM_EVENTS__LOAD].record)
		rec_argv[i++] = "-W";

	rec_argv[i++] = "-d";

	for (j = 0; j < PERF_MEM_EVENTS__MAX; j++) {
		if (!perf_mem_events[j].record)
			continue;

		if (!perf_mem_events[j].supported) {
			pr_err("failed: event '%s' not supported\n",
			       perf_mem_events[j].name);
			return -1;
		}

		rec_argv[i++] = "-e";
		rec_argv[i++] = perf_mem_events[j].name;
	};

	if (all_user)
		rec_argv[i++] = "--all-user";

	if (all_kernel)
		rec_argv[i++] = "--all-kernel";

	for (j = 0; j < argc; j++, i++)
		rec_argv[i] = argv[j];

	if (verbose > 0) {
		pr_debug("calling: record ");

		while (rec_argv[j]) {
			pr_debug("%s ", rec_argv[j]);
			j++;
		}
		pr_debug("\n");
	}

	ret = cmd_record(i, rec_argv, NULL);
	free(rec_argv);
	return ret;
}

int cmd_c2c(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const struct option c2c_options[] = {
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_END()
	};

	argc = parse_options(argc, argv, c2c_options, c2c_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (!argc)
		usage_with_options(c2c_usage, c2c_options);

	if (!strncmp(argv[0], "rec", 3)) {
		return perf_c2c__record(argc, argv);
	} else if (!strncmp(argv[0], "rep", 3)) {
		return perf_c2c__report(argc, argv);
	} else {
		usage_with_options(c2c_usage, c2c_options);
	}

	return 0;
}
