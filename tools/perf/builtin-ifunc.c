
#include <linux/compiler.h>
#include <sys/types.h>
#include <signal.h>

#include "builtin.h"
#include "parse-options.h"
#include "parse-events.h"
#include "debug.h"
#include "target.h"
#include "session.h"
#include "evlist.h"
#include "perf.h"
#include "util.h"

struct perf_ifunc {
	struct perf_record_opts	opts;
	const char		*output;
	struct perf_evlist	*evlist;
	struct perf_session	*session;
};

static struct perf_ifunc perf_ifunc = {
	.opts = {
		.target		 = {
			.uses_mmap	= true,
			.uid		= UINT_MAX,
		},
	},
};

static const struct option ifunc_options[] = {
	OPT_CALLBACK('e', "event", &perf_ifunc.evlist, "event",
		     "event selector. use 'perf list' to list available events",
		     parse_events_option),
	OPT_STRING('o', "output", &perf_ifunc.output, "file",
		    "output file name"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show counter open errors, etc)"),
	OPT_END()
};

static const char * const ifunc_usage[] = {
	"perf ifunc [<options>] [<command>]",
	"perf ifunc [<options>] -- <command> [<options>]",
	NULL
};

static int aux_samples;

static int aux_output(void *buf __maybe_unused, size_t size)
{
	aux_samples++;
	pr_err("got %d.aux output, size %ld\n", aux_samples, size);
	return 0;
}

static int mmap_read(struct perf_mmap *md)
{
	unsigned int head = perf_mmap__read_head(md);
	unsigned int old = md->prev;
	unsigned char *data = md->base + page_size;
	unsigned long size;
	void *buf;
	int rc = 0;

	if (old == head)
		return 0;

	size = head - old;

	if ((old & md->mask) + size != (head & md->mask)) {
		buf = &data[old & md->mask];
		size = md->mask + 1 - (old & md->mask);
		old += size;

		if (aux_output(buf, size) < 0) {
			rc = -1;
			goto out;
		}
	}

	buf = &data[old & md->mask];
	size = head - old;
	old += size;

	if (aux_output(buf, size) < 0) {
		rc = -1;
		goto out;
	}

	md->prev = old;
	perf_mmap__write_tail(md, old);

out:
	return rc;
}

static int mmap_read_all(struct perf_evlist *evlist)
{
	int i;

	for (i = 0; i < evlist->nr_mmaps; i++) {
		if (evlist->mmap[i].base) {
			if (mmap_read(&evlist->mmap[i]) != 0)
				return -1;
		}
	}

	return 0;
}

static struct perf_evlist* aux_evlist(void)
{
	struct perf_evsel *evsel;
	struct perf_evlist *evlist;
	static struct perf_event_attr attr = {
		.type   = PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_ALIGNMENT_FAULTS,
	};

	evlist = perf_evlist__new();
	if (!evlist)
		return NULL;

	event_attr_init(&attr);

	evsel = perf_evsel__new(&attr, 0);
	if (evsel == NULL)
		goto error;

	perf_evlist__add(evlist, evsel);
	return evlist;

 error:
	perf_evlist__delete(evlist);
        return NULL;
}

static volatile int done = 0;
static volatile int child_finished = 0;

static void sig_handler(int sig)
{
	if (sig == SIGCHLD)
		child_finished = 1;

	done = 1;
}

static int __cmd_ifunc(const char **argv, struct perf_ifunc *ifunc)
{
	struct perf_target *target = &ifunc->opts.target;
	struct perf_evlist *evlist;
	struct perf_evsel *evsel;
	int err = -ENOMEM;

	evlist = aux_evlist();
	if (!evlist)
		goto out_free_evlist;

	if (perf_evlist__create_maps(evlist, target) < 0)
		goto out_free_evlist;

	err = perf_evlist__prepare_workload(evlist, target, argv,
					    false, true);
	if (err)
		goto out_free_evlist;

	evsel = perf_evlist__first(evlist);

	perf_evlist__config(evlist, &ifunc->opts);

	if (perf_evsel__open(evsel, evlist->cpus, evlist->threads) < 0)
		goto out_free_evlist;

        if (perf_evlist__mmap(evlist, UINT_MAX, false) < 0)
		goto out_free_evlist;

	perf_evlist__start_workload(evlist);

	for (;;) {
		err = poll(evlist->pollfd, evlist->nr_fds, 100);
		if (err > 0) {
			if (mmap_read_all(evlist) < 0) {
				err = -1;
				goto out_free_evlist;
			}
		} else if (err < 0)
			break;

		if (!err && done)
			break;
	}

	if (!child_finished)
		kill(evlist->workload.pid, SIGTERM);

 out_free_evlist:
	perf_evlist__delete(evlist);
	return err;
}


int cmd_ifunc(int argc, const char **argv,
	      const char *prefix __maybe_unused)
{
	struct perf_evlist *evlist;
	int err = 0;

	signal(SIGCHLD, sig_handler);
	signal(SIGINT,  sig_handler);
	signal(SIGTERM, sig_handler);

	evlist = perf_evlist__new();
	if (!evlist)
		return -ENOMEM;

	perf_ifunc.evlist = evlist;

	argc = parse_options(argc, argv, ifunc_options, ifunc_usage,
			    PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc)
		usage_with_options(ifunc_usage, ifunc_options);

	err = __cmd_ifunc(argv, &perf_ifunc);

	perf_evlist__delete(evlist);
	return err;
}
