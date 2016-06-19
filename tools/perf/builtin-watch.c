#include <linux/compiler.h>
#include <subcmd/parse-options.h>
#include <sys/ioctl.h>
#include <api/fs/fs.h>
#include "builtin.h"
#include "perf.h"
#include "debug.h"
#include "color.h"
#include "thread_map.h"

struct watch_line {
	char			*name;
	char			*data;
	int			 color;
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

#define for_each_token(__buf, __sep, __tmp)		\
	for (tok = strtok_r(__buf, __sep, &__tmp); tok;	\
	     tok = strtok_r(NULL,  __sep, &__tmp))

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

static int add_line(struct watch_items *items, struct watch_item *item, char *str)
{
	struct watch_line *line;
	char *name, *data;

	data = index(str, ':');
	*data++ = 0x0;
	name = trim(str);

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

static int is_sched_item(struct watch *w, char *tok)
{
	size_t len;
	int is_root;

	if (w->rq.type == SCHED_RQ && (!strncmp("cpu#", tok, 4)))
		return 1;

	if (strncmp("cfs_rq[", tok, 7))
		return 0;

	len     = strlen(tok);
	is_root = !strncmp("]:/", tok + len - 3, 3);

	if ((w->rq.type == SCHED_CFS) && !is_root)
		return 1;

	if ((w->rq.type == SCHED_CFS_ROOT) && is_root)
		return 1;

	return 0;
}

static int sched_watch_read(struct watch *w)
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

	old_items = w->items;

	w->items.item = NULL;
	w->items.cnt  = 0;
	w->items.width_data = 0;
	w->items.width_name = 0;

	for_each_token(buf, "\n", tmp) {
		if (is_sched_item(w, tok)) {
			item = new_item(&w->items);
			if (!item)
				return -ENOMEM;
			item->name = rtrim(tok);
			w->items.width_data = max(w->items.width_data, strlen(item->name));
		} else if (!strncmp("  .", tok, 3)) {
			if (!item)
				continue;
			if (add_line(&w->items, item, tok))
				return -ENOMEM;

			if (old_items.item)
				compare_items(&w->items, &old_items);
		} else
			item = NULL;
	}

	free_items(&old_items);
	free(w->rq.buf);
	w->rq.buf = buf;
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

	for_each_token(buf, "\n", tmp) {
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

		line = new_line(item);
		if (!line)
			return -ENOMEM;

		line->name = name;
		line->data = trim(val);

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

static int task_watch_read(struct watch *w)
{
	struct thread_map *m = w->task.pid;
	struct watch_items old_items;
	struct watch_item *item;
	int i;

	old_items = w->items;

	w->items.item = NULL;
	w->items.cnt  = 0;
	w->items.width_data = 0;
	w->items.width_name = 0;

	for (i = 0; i < m->nr; i++) {
		item = new_item(&w->items);
		if (!item)
			return -ENOMEM;

		item->name = strdup(task_name(&m->map[i]));
		w->items.width_data = max(w->items.width_data, strlen(item->name));

		if (read_task(item, m->map[i].pid, &w->items, &old_items))
			return -EINVAL;
	}

	free_items(&old_items);
	return 0;
}

static struct watch watch[] = {
	{
		.name 		= "rq",
		.read		= sched_watch_read,
		.rq.type	= SCHED_RQ,
	},
	{
		.name		= "cfs",
		.read		= sched_watch_read,
		.rq.type	= SCHED_CFS,
	},
	{
		.name		= "cfs_root",
		.read		= sched_watch_read,
		.rq.type	= SCHED_CFS_ROOT
	},
	{
		.name		= "task",
		.read		= task_watch_read,
	},
	{ NULL },
};

static int is_task_watch(struct watch *w)
{
	return w->read == task_watch_read;
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
	int cols, items, lines, i;

	/* for readability */
	w->items.width_data++;
	w->items.width_name++;

	items = w->items.cnt;
	lines = w->items.item[0].cnt + 1;

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
	const char *pid = NULL;
	const struct option options[] = {
		OPT_INCR('v', "verbose", &verbose,
			 "be more verbose (show counter open errors, etc)"),
		OPT_STRING('p', "pid", &pid, "pid", "pids"),
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

	if (pid) {
		if (!is_task_watch(w))
			return -1;
		w->task.pid = thread_map__new_str(pid, NULL, 0);
		if (!w->task.pid)
			return -1;
	}

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
