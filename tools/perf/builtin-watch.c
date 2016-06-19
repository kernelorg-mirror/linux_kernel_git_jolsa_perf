#include <linux/compiler.h>
#include <subcmd/parse-options.h>
#include "builtin.h"
#include "perf.h"
#include "debug.h"

struct watch;

struct watch_line {
	char			*name;
	int			 idx;

	char			*data;
};

struct watch_item {
	char			*name;
	int			 idx;

	struct watch_line	*line;
	int			 cnt;

	struct watch		*watch;
};

struct watch_items {
	struct watch_item	*item;
	int			 cnt;
};

typedef int (*watch_read_fn_t)(struct watch *);

struct watch {
	const char		*name;
	const char		*help;
	watch_read_fn_t		 read;

	struct watch_items	 items;
};

static struct watch watch[] = {
	{ NULL },
};

static void watch_help(void)
{
	struct watch *w = &watch[0];

	fprintf(stderr, "\nAvailable commands:\n");

	while (w->name) {
		fprintf(stderr, "%11s - %s\n", w->name, w->help);
		w++;
	}
}

static struct watch *find_watch(const char *name)
{
	struct watch *w = &watch[0];

	while (w->name) {
		if (!strcmp(name, w->name))
			return w;
		w++;
	}

	return NULL;
}

int cmd_watch(int argc __maybe_unused, const char **argv __maybe_unused)
{
	const struct option options[] = {
		OPT_INCR('v', "verbose", &verbose,
			 "be more verbose (show counter open errors, etc)"),
		OPT_END()
	};
	const char *usage[] = {
		"perf watch <command> [<options>]",
		NULL
	};
	const char *name;
	struct watch *w;

	if (argc < 2) {
		watch_help();
		usage_with_options(usage, options);
	}

	name = argv[1];

	w = find_watch(name);
	if (!w) {
		pr_err("failed: '%s' unsupported\n", name);
		watch_help();
		usage_with_options(usage, options);
	}

	argc = parse_options_subcommand(argc, argv, options, NULL, usage,
					PARSE_OPT_KEEP_UNKNOWN);
	if (argc < 1)
		usage_with_options(usage, options);

	while (1) {
		int ret;

		ret = w->read(w);
		if (ret) {
			pr_err("failed: reading data\n");
			return -1;
		}
		sleep(1);
	}

	return 0;
}
