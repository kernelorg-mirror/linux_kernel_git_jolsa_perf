#include <linux/compiler.h>
#include <subcmd/parse-options.h>
#include "builtin.h"
#include "perf.h"

static const struct option watch_options[] = {
	OPT_END()
};

static const char * const watch_data[] = { NULL };

static const char *watch_usage[] = {
	"perf watch [<options>] <data>",
	NULL
};

struct watch_line {
	char *name;
	char *data;
};

struct watch_item {
	char			*name;
	struct watch_line	*lines;
	int			 lines_cnt;
};

struct watch_data;

typedef int (*watch_read_fn_t)(struct watch_data *);

struct watch_data {
	const char		*name;
	watch_read_fn_t		 read;

	struct watch_item	*items;
	int			 items_cnt;

};

static int watch_read(struct watch_data *data __maybe_unused)
{
	return 0;
}

static struct watch_data data[] __maybe_unused = {
	{ .name = "rq", .read = watch_read },
	{ NULL },
};

static struct watch_data *find_watch(const char *name)
{
	struct watch_data *watch = &data[0];

	while (watch->name) {
		if (!strcmp(name, watch->name))
			return watch;
		watch++;
	}

	return NULL;
}

static int display_watch(struct watch_data *watch __maybe_unused)
{
	return 0;
}

int cmd_watch(int argc, const char **argv,
	      const char *prefix __maybe_unused)
{
	struct watch_data *watch;

	/* No command specified. */
	if (argc < 2)
		return -1;

	argc = parse_options_subcommand(argc, argv, watch_options, watch_data, watch_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc < 1)
		return -1;

	watch = find_watch(argv[0]);
	if (!watch)
		return -1;

	while (1) {
		watch->read(watch);
		display_watch(watch);
	}

	return 0;
}
