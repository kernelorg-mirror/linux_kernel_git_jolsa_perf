#include <linux/compiler.h>
#include <subcmd/parse-options.h>
#include <sys/stat.h>
#include <api/fs/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include "builtin.h"
#include "perf.h"
#include "color.h"

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
	int   color;
};

struct watch_item {
	char			*name;
	struct watch_line	*line;
	int			 cnt;
};

struct watch_items {
	struct watch_item *item;
	int		   cnt;
};

struct watch_data;

typedef int (*watch_read_fn_t)(struct watch_data *);

struct watch_data {
	const char		*name;
	watch_read_fn_t		 read;

	struct watch_items	 items;
	char			*buf;
};

static struct watch_item* new_item(struct watch_items *items)
{
	struct watch_item *item;
	size_t size = sizeof(*item);

	items->cnt++;

	if (items->item) 
		size *= items->cnt;

	items->item = realloc(items->item, size);
	if (!items->item)
		return NULL;

	item = &items->item[items->cnt - 1];
	memset(item, 0, sizeof(*item));
	return item;
}

static struct watch_line* new_line(struct watch_item *item)
{
	struct watch_line *line;
	size_t size = sizeof(*line);

	item->cnt++;

	if (item->line) 
		size *= item->cnt;

	item->line = realloc(item->line, size);
	if (!item->line)
		return NULL;

	line = &item->line[item->cnt - 1];
	memset(line, 0, sizeof(*line));
	return line;
}

static int add_line(struct watch_item *item, char *str)
{
	struct watch_line *line;
	char *name, *data;

	line = new_line(item);
	if (!line)
		return -ENOMEM;

	name = rtrim(str);
	data = index(str, ':');
	*data++ = 0x0;

	line->name = rtrim(name);
	line->data = data;
	return 0;
}

static void free_items(struct watch_items *items)
{
	int i;

	for (i = 0; i < items->cnt; i++)
		free(items->item[i].line);

	free(items->item);
}

static void compare_lines(struct watch_line *new,
			  struct watch_line *old)
{
	int equal = 0;

	if (!strcmp(new->name, old->name) &&
	    !strcmp(new->data, old->data))
		equal = 1;

	if (equal && old->color)
		new->color = old->color - 1;

	if (!equal)
		new->color = 5;
}

static void compare_items(struct watch_items *new,
			  struct watch_items *old)
{
	struct watch_item *item_new, *item_old;
	int i, l;

	i = new->cnt - 1;
	item_new = &new->item[i];
	item_old = &old->item[i];

	l = item_new->cnt - 1;
	compare_lines(&item_new->line[l], &item_old->line[l]);
}

static int watch_read(struct watch_data *data)
{
	struct watch_items old_items;
	char *tok, *tmp = NULL;
	char path[PATH_MAX];
	struct watch_item *item = NULL;
	char *buf;
	size_t size;

	scnprintf(path, PATH_MAX, "%s/sched_debug", procfs__mountpoint());

	if (filename__read_str(path, &buf, &size))
		return -1;

	old_items = data->items;

	data->items.item = NULL;
	data->items.cnt  = 0;

	for (tok = strtok_r(buf, "\n", &tmp); tok;
	     tok = strtok_r(NULL, "\n", &tmp)) {

		if (!strncmp("cpu#", tok, 4)) {
			item = new_item(&data->items);
			if (!item)
				return -ENOMEM;
			item->name = rtrim(tok);
		} else if (!strncmp("  .", tok, 3)) {
			if (!item)
				continue;
			if (add_line(item, tok))
				return -ENOMEM;

			if (old_items.item)
				compare_items(&data->items, &old_items);
		} else
			item = NULL;
	}

	free_items(&old_items);

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

static int display_watch(struct watch_data *watch)
{
	struct watch_item *item0 = &watch->items.item[0];
	bool first = true;
	int i, j;

	for (j = 0; j < item0->cnt; j++) {
		if (first)
			printf("%30s", " ");
		else
			printf("%30s", item0->line[j].name);

		for (i = 0; i < watch->items.cnt; i++) {
			struct watch_item *item = &watch->items.item[i];

			if (first) {
				printf("%30s", item->name);
			} else {
				struct watch_line *line = &item->line[j];

				if (line->color)
					color_fprintf(stdout, PERF_COLOR_GREEN, "%30s", line->data);
				else
					printf("%30s", line->data);
			}

		}
		printf("\n");

		first = false;
	}

	return 0;
}

static void clear_screen(void)
{
	int ret __maybe_unused = system("clear");
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
		clear_screen();
		watch->read(watch);
		display_watch(watch);
		sleep(1);
	}

	return 0;
}
