#include <linux/compiler.h>
#include <subcmd/parse-options.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include "builtin.h"
#include "perf.h"
#include "debug.h"
#include "color.h"
#include "term.h"

struct watch;

#define MAX_LINES 100

struct watch_line {
	char			*name;
	int			 idx;

	char			*data;
	int			 color;
};

struct watch_item {
	char			*name;
	int			 idx;

	struct watch_line	*line;
	int			 cnt;
	int			 cnt_iter;

	struct watch		*watch;
};

struct watch_items {
	struct watch_item	*item;
	int			 cnt;
	int			 cnt_iter;

	size_t			 width_name;
	size_t			 width_data;
};

typedef int (*watch_read_fn_t)(struct watch *);

struct watch {
	const char		*name;
	const char		*help;
	watch_read_fn_t		 read;

	struct watch_items	 items;
};

#define for_each_token(__tok, __buf, __sep, __tmp)		\
	for (__tok = strtok_r(__buf, __sep, &__tmp); __tok;	\
	     __tok = strtok_r(NULL,  __sep, &__tmp))

__maybe_unused
static void items_width(struct watch_items *items,
			struct watch_line *line)
{
	items->width_name = max(items->width_name, strlen(line->name));
	items->width_data = max(items->width_data, strlen(line->data));
}

__maybe_unused
static struct watch_item* new_item(struct watch *w, char *name)
{
	struct watch_items *items = &w->items;
	struct watch_item *item;
	size_t size = sizeof(*item);
	bool allocated = false;

	if (!name)
		return NULL;

	if (items->cnt_iter == items->cnt) {
		items->cnt++;

		if (items->item)
			size *= items->cnt;

		items->item = item = realloc(items->item, size);
		if (!item)
			return NULL;

		allocated = true;
	}

	item = &items->item[items->cnt_iter++];

	if (allocated) {
		/* New item.. */
		memset(item, 0, sizeof(*item));
		item->name  = strdup(name);
		item->idx   = items->cnt_iter - 1;
		item->watch = w;

		item->line = zalloc(sizeof(struct watch_line) * MAX_LINES);
		if (!item->line)
			return NULL;

	} else {
		/* Old item.. restart line iteration. */
		item->cnt_iter = 0;

		/* Item possition/name changed, fail for now. */
		if (strcmp(name, item->name))
			return NULL;
	}

	items->width_data = max(items->width_data, strlen(item->name));
	return item;
}

#define MAX_DATALEN 30

__maybe_unused
static struct watch_line* new_line(struct watch_item *item,
				   char *name, char *data)
{
	struct watch_line *line;

	if (item->cnt_iter == MAX_LINES)
		return NULL;

	if (strlen(data) > MAX_DATALEN) {
		data[MAX_DATALEN] = 0x0;
		data[MAX_DATALEN - 1] = '.';
		data[MAX_DATALEN - 2] = '.';
		data[MAX_DATALEN - 3] = '.';
	}

	line = &item->line[item->cnt_iter];

	if (item->cnt_iter == item->cnt) {
		/* New line. */
		line->name  = strdup(name);
		line->color = 0;
		line->idx   = item->cnt;
		item->cnt++;
	} else {
		/* Old line. */
		int equal = 0;

		/* Line possition/name changed, fail for now. */
		if (strcmp(line->name, name))
			return NULL;

		if (!strcmp(line->data, data))
			equal = 1;

		if (equal && line->color)
			line->color -= 1;

		if (!equal)
			line->color = 3;
	}

	line->data = data;

	item->cnt_iter++;
	return line;
}

__maybe_unused
static void zero_items(struct watch_items *items)
{
	items->cnt_iter   = 0;
	items->width_data = 0;
	items->width_name = 0;
}

static void free_item(struct watch_item *item)
{
	int i;

	for (i = 0; i < MAX_LINES; i++)
		free(item->line[i].name);

	free(item->line);
	free(item->name);
}

static void free_items(struct watch_items *items)
{
	int i;

	for (i = 0; i < items->cnt; i++)
		free_item(&items->item[i]);

	free(items->item);
}

static void free_watch(struct watch *w)
{
	free_items(&w->items);
}

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

static struct winsize ws;
static int rows_from;
static int rows;
static int vrows;
static int lines_idx;

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

static void display_items(struct watch_items *items, int from, int to, int lines_from, int lines_to)
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

	for (j = lines_from; j < lines_to; j++) {
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
	int lines_idx_max, lines_from, lines_to;
	int cols, items, lines, i;

	/* for readability */
	w->items.width_data++;
	w->items.width_name++;

	items = w->items.cnt;
	lines = item0->cnt + 1;

	cols  = min(items, (int) (ws.ws_col - w->items.width_name) / (int) w->items.width_data);
	cols  = max(cols, 1);

	rows  = items / cols;
	rows += items % cols ? 1 : 0;

	/*
	 * TODO -1 for final '\n', detect last
	 * item and do not print last '\n'
	 */
	vrows = (ws.ws_row - 1) / lines;
	vrows = max(vrows, 1);

	lines_idx_max = (item0->cnt / ws.ws_row);
	lines_idx = min(lines_idx, lines_idx_max);
	lines_idx = ws.ws_row > item0->cnt ? 0 : lines_idx;

	lines_from = lines_idx * ws.ws_row;
	lines_from = max(0, lines_from - 2);
	lines_to   = min(lines_from + ws.ws_row - 2, item0->cnt);

	for (i = rows_from; i < rows_from + vrows; i++) {
		int from = i * cols;
		int to   = min(from + cols, items);

		if (from >= items)
			break;

		display_items(&w->items, from, to, lines_from, lines_to);
	}
}

#define gotoxy(x,y) printf("\033[%d;%dH", (x), (y))

static void display_watch(struct watch *w)
{
	get_term_dimensions(&ws);
	gotoxy(0, 0);

	__display_watch(w);
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
	struct termios old;
	bool quit = false;
	const char *name;
	struct watch *w;
	int ret;

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

	ret = system("clear");
	set_term_quiet_input(&old);

	while (!quit) {
		int c;

		ret = w->read(w);
		if (ret) {
			pr_err("failed: reading data\n");
			break;
		}

		display_watch(w);

		c = ui__getch(1);
		switch (c) {
		case 'q':
			quit = true;
			break;
		case 60:
			update_rows(false);
			break;
		case 62:
			update_rows(true);
			break;
		case ' ':
			lines_idx++;
			ret = system("clear");
			break;
		case 'w':
			lines_idx = max(0, lines_idx - 1);
			ret = system("clear");
			break;
		default:
			break;
		}
	}

	tcsetattr(0, TCSAFLUSH, &old);
	free_watch(w);
	return ret;
}
