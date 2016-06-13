#include <linux/compiler.h>
#include <linux/kernel.h>
#include <sys/ioctl.h>
#include <subcmd/parse-options.h>
#include <sys/stat.h>
#include <api/fs/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <search.h>
#include "builtin.h"
#include "perf.h"
#include "color.h"
#include "util.h"
#include "ui/util.h"
#include "ui/keysyms.h"
#include "thread_map.h"
#include "debug.h"

struct watch_line {
	char *name;
	char *data;
	int   color;
};

struct watch_item {
	char			*name;
	struct watch_line	*line;
	int			 cnt;

	union {
		struct {
			char *buf;
		} task;
	};
};

struct watch_items {
	struct watch_item *item;
	int		   cnt;
	size_t		   width_name;
	size_t		   width_data;

	bool		    has_fields;
	struct hsearch_data fields;
};

struct watch_data;

typedef int (*watch_read_fn_t)(struct watch_data *);

struct watch_data {
	const char		*name;
	watch_read_fn_t		 read;

	struct watch_items	 items;

	union {
		struct {
			int	 type;
			char	*buf;
		} rq;
		struct {
			struct thread_map *pid;
		} task;
	};
};

static int is_allowed(struct watch_items *items, char *name)
{
	ENTRY e, *ep;

	if (!items->has_fields)
		return 1;

	e.key = name;
	return hsearch_r(e, FIND, &ep, &items->fields);
}

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

static int add_line(struct watch_items *items, struct watch_item *item, char *str, int *skip)
{
	struct watch_line *line;
	char *name, *data;

	data = index(str, ':');
	*data++ = 0x0;
	name = rtrim(ltrim(str));

	if (!is_allowed(items, name)) {
		*skip = 1;
		return 0;
	}

	line = new_line(item);
	if (!line)
		return -ENOMEM;

	line->name = name;
	line->data = data;

	items->width_name = max(items->width_name, strlen(line->name));
	items->width_data = max(items->width_data, strlen(line->data));
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
		new->color = 3;
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

enum {
	SCHED_RQ,
	SCHED_CFS,
	SCHED_CFS_ROOT,
};

static int is_sched_item(struct watch_data *data, char *tok)
{
	size_t len;
	int is_root;

	if (data->rq.type == SCHED_RQ && (!strncmp("cpu#", tok, 4)))
		return 1;

	if (strncmp("cfs_rq[", tok, 7))
		return 0;

	len     = strlen(tok);
	is_root = !strncmp("]:/", tok + len - 3, 3);

	if ((data->rq.type == SCHED_CFS) && !is_root)
		return 1;

	if ((data->rq.type == SCHED_CFS_ROOT) && is_root)
		return 1;

	return 0;
}

static int watch_sched_read(struct watch_data *data)
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
	data->items.width_data = 0;
	data->items.width_name = 0;

	for (tok = strtok_r(buf, "\n", &tmp); tok;
	     tok = strtok_r(NULL, "\n", &tmp)) {
		int skip = 0;

		if (is_sched_item(data, tok)) {
			item = new_item(&data->items);
			if (!item)
				return -ENOMEM;
			item->name = rtrim(tok);
			data->items.width_data = max(data->items.width_data, strlen(item->name));
		} else if (!strncmp("  .", tok, 3)) {
			if (!item)
				continue;
			if (add_line(&data->items, item, tok, &skip))
				return -ENOMEM;

			if (skip)
				continue;

			if (old_items.item)
				compare_items(&data->items, &old_items);
		} else
			item = NULL;
	}

	free_items(&old_items);
	free(data->rq.buf);
	data->rq.buf = buf;
	return 0;
}

static int read_task(struct watch_item *item, int tid,
		     struct watch_items *new_items,
		     struct watch_items *old_items)
{
	char *tok, *tmp = NULL;
	char path[PATH_MAX];
	char *buf;
	size_t size;
	int start = 0;

	scnprintf(path, PATH_MAX, "%s/%d/sched", procfs__mountpoint(), tid);

	if (filename__read_str(path, &buf, &size))
		return -1;

	for (tok = strtok_r(buf, "\n", &tmp); tok;
	     tok = strtok_r(NULL, "\n", &tmp)) {
		struct watch_line *line;
		char *name;
		char *val;

		if (!start) {
			if (*tok != '-')
				continue;
			start = 1;
			continue;
		}

		val = index(tok, ':');
		if (!val)
			continue;

		*val++ = 0x0;
		name = rtrim(tok);
		if (!is_allowed(new_items, name))
			continue;

		line = new_line(item);
		if (!line)
			return -ENOMEM;

		line->name = name;
		line->data = ltrim(rtrim(val));

		new_items->width_name = max(new_items->width_name, strlen(line->name));
		new_items->width_data = max(new_items->width_data, strlen(line->data));

		if (old_items->item)
			compare_items(new_items, old_items);
	}

	free(item->task.buf);
	item->task.buf = buf;
	return 0;
}

static char *task_name(struct thread_map_data *m)
{
	static char buf[50];

	scnprintf(buf, 50, "%s-%d", m->comm ? rtrim(m->comm) : "pid", m->pid);
	return buf;
}

static int watch_task_read(struct watch_data *watch)
{
	struct thread_map *m = watch->task.pid;
	struct watch_items old_items;
	struct watch_item *item;
	int i;

	old_items = watch->items;

	watch->items.item = NULL;
	watch->items.cnt  = 0;
	watch->items.width_data = 0;
	watch->items.width_name = 0;

	for (i = 0; i < m->nr; i++) {
		item = new_item(&watch->items);
		if (!item)
			return -ENOMEM;

		item->name = strdup(task_name(&m->map[i]));
		watch->items.width_data = max(watch->items.width_data, strlen(item->name));

		if (read_task(item, m->map[i].pid, &watch->items, &old_items))
			return -EINVAL;
	}

	free_items(&old_items);
	return 0;
}

static struct watch_data data[] = {
	{
		.name 		= "rq",
		.read		= watch_sched_read,
		.rq.type	= SCHED_RQ,
	},
	{
		.name		= "cfs",
		.read		= watch_sched_read,
		.rq.type	= SCHED_CFS,
	},
	{
		.name		= "cfs_root",
		.read		= watch_sched_read,
		.rq.type	= SCHED_CFS_ROOT
	},
	{
		.name		= "task",
		.read		= watch_task_read,
	},
	{ NULL },
};

static int is_task_watch(struct watch_data *watch)
{
	return watch->read == watch_task_read;
}

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

static void display_watch(struct watch_data *watch)
{
	int cols, items, lines, i;

	/* for readability */
	watch->items.width_data++;
	watch->items.width_name++;

	items = watch->items.cnt;
	lines = watch->items.item[0].cnt + 1;

	cols  = min(items, (int) (ws.ws_col - watch->items.width_name) / (int) watch->items.width_data);
	rows  = items / cols;
	rows += items % cols ? 1 : 0;

	vrows = ws.ws_row / lines;

	for (i = rows_from; i < rows_from + vrows; i++) {
		int from = i * cols;
		int to   = min(from + cols, items);

		if (from >= items)
			break;

		display_items(&watch->items, from, to);
	}
}

#define gotoxy(x,y) printf("\033[%d;%dH", (x), (y))

static void clear_screen(void)
{
//	int ret __maybe_unused = system("clear");
	gotoxy(0, 0);
}

static void sig_winch(int sig __maybe_unused,
		      siginfo_t *info __maybe_unused,
		      void *arg __maybe_unused)
{
	get_term_dimensions(&ws);
}

static const char *watch_usage[] = {
	"perf watch [<options>] <data>",
	NULL
};

#define MAX_FIELDS 50
static int setup_fields(struct watch_data *watch, const char *field)
{
	char *tok, *tmp = NULL;
	ENTRY e, *ep;
	char *buf = strdup(field);
	struct stat st;
	const char *sep = ",";

	if (!buf)
		return -1;

	if (!stat(field, &st)) {
		char *bufh;
		size_t size;

		if (filename__read_str(buf, &bufh, &size))
			return -1;

		buf = bufh;
		sep = "\n";
	}

	if (!hcreate_r(MAX_FIELDS, &watch->items.fields))
		return -1;

	for (tok = strtok_r(buf, sep, &tmp); tok;
	     tok = strtok_r(NULL, sep, &tmp)) {
		e.key  = tok;
		e.data = NULL;

		if (!hsearch_r(e, ENTER, &ep, &watch->items.fields))
			return -1;
	}

	watch->items.has_fields = true;
	return 0;
}

int cmd_watch(int argc, const char **argv,
	      const char *prefix __maybe_unused)
{
	struct watch_data *watch;
	const char *pid = NULL;
	const char *field = NULL;
	const struct option watch_options[] = {
		OPT_INCR('v', "verbose", &verbose,
			 "be more verbose (show counter open errors, etc)"),
		OPT_STRING('p', "pid", &pid, "pid", "pids"),
		OPT_STRING('f', "field", &field, "field", "fields"),
		OPT_END()
	};
	struct sigaction act = {
		.sa_sigaction	= sig_winch,
		.sa_flags	= SA_SIGINFO,
	};
	struct termios old;
	int ret __maybe_unused;

	/* No command specified. */
	if (argc < 2)
		return -1;

	argc = parse_options_subcommand(argc, argv, watch_options, NULL, watch_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc < 1)
		return -1;

	watch = find_watch(argv[0]);
	if (!watch)
		return -1;

	if (pid) {
		if (!is_task_watch(watch))
			return -1;
		watch->task.pid = thread_map__new_str(pid, NULL, 0);
		if (!watch->task.pid)
			return -1;
	}

	if (field && setup_fields(watch, field))
		return -1;

	get_term_dimensions(&ws);
	sigaction(SIGWINCH, &act, NULL);

	ret = system("clear");
	set_term_quiet_input(&old);

	while (1) {
		int k;

		clear_screen();
		get_term_dimensions(&ws);
		watch->read(watch);
		display_watch(watch);

		k = ui__getch(1);
		switch (k) {
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
