#include <linux/compiler.h>
#include <subcmd/parse-options.h>
#include <sys/ioctl.h>
#include "builtin.h"
#include "perf.h"
#include "debug.h"
#include "color.h"

struct watch_line {
	char			*name;
	char			*data;
	int			 color;
};

struct watch_item {
	char			*name;
	struct watch_line	*line;
	int			 cnt;
};

struct watch_items {
	struct watch_item	*item;
	int			 cnt;
	size_t			 width_name;
	size_t			 width_data;
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


static struct winsize ws;
static int rows_from;
static int rows;
static int vrows;

static void update_rows(bool inc)
{
	int ret __maybe_unused;

	ret = system("clear");

	if (inc) {
		if ((rows_from + vrows) < rows)
			rows_from++;
	} else {
		if (rows_from > 0)
			rows_from--;
	}
}

static void display_items(struct watch_items *items, int from, int to)
{
	struct watch_item *item0 = &items->item[0];
	int width_name = (int) items->width_name;
	int width_data = (int) items->width_data;
	int i, j;

	/* header */
	printf("%-*s", width_name, " ");

	for (i = from; i < to; i++) {
		struct watch_item *item = &items->item[i];
		color_fprintf(stdout, PERF_COLOR_YELLOW, "%*s", width_data, item->name);
	}

	printf("\n");

	for (j = 0; j < item0->cnt; j++) {
		printf("%-*s", width_name, item0->line[j].name);

		for (i = from; i < to; i++) {
			struct watch_item *item = &items->item[i];
			struct watch_line *line = &item->line[j];

			if (line->color) {
				const char *color = line->color == 3 ?
						    PERF_COLOR_RED : PERF_COLOR_GREEN;

				color_fprintf(stdout, color, "%*s", width_data, line->data);
			} else {
				printf("%*s", width_data, line->data);
			}

		}
		printf("\n");
	}
}

static void __display_watch(struct watch *w)
{
	struct watch_item *item0 = &w->items.item[0];
	int cols, items, lines, i;

	/* for readability */
	w->items.width_data++;
	w->items.width_name++;

	items = w->items.cnt;
	lines = item0->cnt + 1;

	cols  = min(items, (int) (ws.ws_col - w->items.width_name) / (int) w->items.width_data);
	rows  = items / cols;
	rows += items % cols ? 1 : 0;

	vrows = ws.ws_row / lines;

	for (i = rows_from; i < rows_from + vrows; i++) {
		int from = i * cols;
		int to   = min(from + cols, items);

		if (from >= items)
			break;

		display_items(&w->items, from, to);
	}
}

#define gotoxy(x,y) printf("\033[%d;%dH", (x), (y))

static void display_watch(struct watch *w)
{
	get_term_dimensions(&ws);
	gotoxy(0, 0);

	__display_watch(w);
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
	struct termios old;
	const char *name;
	struct watch *w;
	int ret;

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

	ret = system("clear");
	set_term_quiet_input(&old);

	while (1) {
		int c;

		ret = w->read(w);
		if (ret) {
			pr_err("failed: reading data\n");
			return -1;
		}

		display_watch(w);

		c = ui__getch(1);
		switch (c) {
		case 60:
			update_rows(false);
			break;
		case 62:
			update_rows(true);
		default:
			break;
		}
	}

	return 0;
}
