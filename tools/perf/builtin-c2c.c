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

struct perf_c2c {
	struct perf_tool	tool;
	struct hists		hists;
	struct perf_hpp_list	hpp_list;
};

static int perf_c2c_hists_init(struct perf_c2c *c2c)
{
	__hists__init(&c2c->hists);
	c2c->hists.hpp_list = &c2c->hpp_list;
	perf_hpp_list__init(&c2c->hpp_list);
	return hists__setup_hpp_list(&c2c->hists, "c2c_dcacheline", "c2c_dcacheline");
}

static struct c2c_hists* get_c2c_hists(struct hist_entry *he)
{
	struct c2c_hists *c2c_hists = he->c2c_hists;

	if (!c2c_hists) {
		he->c2c_hists = c2c_hists = malloc(sizeof(*c2c_hists));
		if (!c2c_hists)
			return NULL;

		__hists__init(&c2c_hists->hists);
		c2c_hists->hists.hpp_list = &c2c_hists->hpp_list;

		perf_hpp_list__init(&c2c_hists->hpp_list);
		hists__setup_hpp_list(&c2c_hists->hists, "c2c_offset,symbol_daddr,dso_daddr,symbol_iaddr,pid,dso", NULL);
	}

	return c2c_hists;
}

static struct hist_entry*
__he__add_c2c_entry(struct hists *hists, struct hist_entry *entry)
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
	return he;
}

static struct hist_entry*
he__add_c2c_entry(struct hist_entry *he, struct hist_entry *entry)
{
	struct c2c_hists *c2c_hists = get_c2c_hists(he);

	if (!c2c_hists)
		return NULL;

	return __he__add_c2c_entry(&c2c_hists->hists, entry);
}

static struct hist_entry*
__hists__add_c2c_entry(struct hists *hists, struct hist_entry *entry)
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
	if (!he__add_c2c_entry(he, entry))
		return NULL;

	return he;
}

static struct hist_entry*
hists__add_c2c_entry(struct hists *hists, struct addr_location *al,
		     struct mem_info *mi)
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

	return __hists__add_c2c_entry(hists, &entry);
}

static int process_sample_event(struct perf_tool *tool,
				union perf_event *event,
				struct perf_sample *sample,
				struct perf_evsel *evsel __maybe_unused,
				struct machine *machine)
{
	struct perf_c2c *c2c = container_of(tool, struct perf_c2c, tool);
	struct addr_location al;
	struct mem_info *mi;
	struct hist_entry *he;

	if (perf_event__preprocess_sample(event, machine, &al, sample) < 0) {
		fprintf(stderr, "problem processing %d event, skipping it.\n",
				event->header.type);
		return -1;
	}

	mi = sample__resolve_mem(sample, &al);
	if (mi == NULL)
		return -ENOMEM;

	he = hists__add_c2c_entry(&c2c->hists, &al, mi);
	if (!he)
		return -1;

	return 0;
}

static const char * const c2c_usage[] = {
	"perf c2c {record|report}",
	NULL
};

static const char * const __usage_report[] = {
	"perf c2c report",
	NULL
};

static const char * const *report_c2c_usage = __usage_report;

static int perf_c2c_browse_report(struct perf_c2c *c2c)
{
        struct rb_node *nd;
	hists__output_resort(&c2c->hists, NULL);

        nd = rb_first(&c2c->hists.entries);
	do {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);
		struct c2c_hists *c2c_hists = he->c2c_hists;

		hists__output_resort(&c2c_hists->hists, NULL);
                nd = rb_next(nd);
        } while (nd);

	return 0;
}

static int perf_c2c__report(int argc, const char **argv)
{
	struct perf_session *session;
	struct perf_c2c c2c = {
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
	};
	struct perf_data_file file = {
		.path = input_name,
		.mode = PERF_DATA_MODE_READ,
	};
	const struct option c2c_options[] = {
	OPT_INCR('v', "verbose", &verbose,
		 "be more verbose (show counter open errors, etc)"),
	OPT_STRING('i', "input", &input_name, "file",
		   "the input file to process"),
	OPT_END()
	};
	int err = -1;

	use_browser = 1;
	setup_browser(false);

	if (perf_c2c_hists_init(&c2c))
		return -1;

	argc = parse_options(argc, argv, c2c_options, report_c2c_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	session = perf_session__new(&file, 0, &c2c.tool);
	if (session == NULL) {
		pr_debug("No memory for session\n");
		goto out;
	}

	err = perf_session__process_events(session);

	if (!err)
		err = perf_c2c_browse_report(&c2c);

	perf_session__delete(session);
out:
	return err;
}

static int parse_record_events(const struct option *opt __maybe_unused,
			       const char *str, int unset __maybe_unused)
{
	bool *event_set = *(bool**)opt->value;
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
