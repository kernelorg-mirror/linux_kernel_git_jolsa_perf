#include <linux/compiler.h>
#include <subcmd/parse-options.h>
#include "builtin.h"
#include "perf.h"
#include "debug.h"

struct watch_line {
	char			*name;
	char			*data;
};

struct watch_item {
	char			*name;
	struct watch_line	*line;
	int			 cnt;
};

struct watch_items {
	struct watch_item	*item;
	int			 cnt;
};

struct watch;
typedef int (*watch_read_fn_t)(struct watch *);

struct watch {
	const char		*name;
	watch_read_fn_t		 read;

	struct watch_items	 items;
};

static struct watch watch[] = {
	{ NULL },
};

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

int cmd_watch(int argc, const char **argv,
	      const char *prefix __maybe_unused)
{
	const struct option options[] = {
		OPT_INCR('v', "verbose", &verbose,
			 "be more verbose (show counter open errors, etc)"),
		OPT_END()
	};
	const char *usage[] = {
		"perf watch <name> [<options>]",
		NULL
	};
	const char *name;
	struct watch *w;

	if (argc < 2)
		usage_with_options(usage, options);

	name = argv[1];

	w = find_watch(name);
	if (!w) {
		pr_err("failed: watch '%s' unsupported\n", name);
		return -1;
	}

	argc = parse_options_subcommand(argc, argv, options, NULL, usage,
					PARSE_OPT_KEEP_UNKNOWN);
	if (argc < 1)
		return -1;

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
