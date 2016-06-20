#include <linux/compiler.h>
#include <subcmd/parse-options.h>
#include <sys/ioctl.h>
#include <api/fs/fs.h>
#include <search.h>
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
	bool			 has_fields;
	struct hsearch_data	 fields;
};

struct watch;
typedef int (*watch_read_fn_t)(struct watch *);

enum {
	WATCH_TASK = 1 << 0,
};

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

	u64			  flags;
	const char		 *help;
};

#define for_each_token(__buf, __sep, __tmp)		\
	for (tok = strtok_r(__buf, __sep, &__tmp); tok;	\
	     tok = strtok_r(NULL,  __sep, &__tmp))

static int is_allowed(struct watch_items *items, char *name)
{
	ENTRY e, *ep;

	if (!items->has_fields)
		return 1;

	e.key = name;
	return hsearch_r(e, FIND, &ep, &items->fields);
}

static void items_width(struct watch_items *items,
			struct watch_line *line)
{
	items->width_name = max(items->width_name, strlen(line->name));
	items->width_data = max(items->width_data, strlen(line->data));
}

static struct watch_item* new_item(struct watch_items *items,
				   char *name)
{
	struct watch_item *item;
	size_t size = sizeof(*item);

	if (!name)
		return NULL;

	items->cnt++;

	if (items->item)
		size *= items->cnt;

	items->item = item = realloc(items->item, size);
	if (!item)
		return NULL;

	item += items->cnt - 1;
	memset(item, 0, sizeof(*item));

	item->name = strdup(name);
	items->width_data = max(items->width_data, strlen(name));
	return item;
}

static struct watch_line* new_line(struct watch_item *item,
				   char *name, char *data)
{
	struct watch_line *line;
	size_t size = sizeof(*line);

	item->cnt++;

	if (item->line)
		size *= item->cnt;

	item->line = line = realloc(item->line, size);
	if (!line)
		return NULL;

	line += item->cnt - 1;
	line->name  = name;
	line->data  = data;
	line->color = 0;

	return line;
}

static int add_line(struct watch_items *items, struct watch_item *item, char *str, int *skip)
{
	struct watch_line *line;
	char *name, *data;

	data = index(str, ':');
	*data++ = 0x0;
	name = trim(str);

	if (!is_allowed(items, name)) {
		*skip = 1;
		return 0;
	}

	line = new_line(item, name, data);
	if (!line)
		return -ENOMEM;

	items_width(items, line);
	return 0;
}

static void zero_items(struct watch_items *items)
{
	items->item = NULL;
	items->cnt  = 0;
	items->width_data = 0;
	items->width_name = 0;
}

static void free_items(struct watch_items *items)
{
	int i;

	for (i = 0; i < items->cnt; i++) {
		free(items->item[i].name);
		free(items->item[i].line);
	}

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
	SCHED_RT,
	SCHED_DL,
};

static int is_sched_item(struct watch *w, char *tok)
{
	size_t len;
	int is_root;

	if (w->rq.type == SCHED_RQ && (!strncmp("cpu#", tok, 4)))
		return 1;

	if (!strncmp("cfs_rq[", tok, 7)) {
		len     = strlen(tok);
		is_root = !strncmp("]:/", tok + len - 3, 3);

		if ((w->rq.type == SCHED_CFS) && !is_root)
			return 1;

		if ((w->rq.type == SCHED_CFS_ROOT) && is_root)
			return 1;

		return 0;
	}

	if (!strncmp("rt_rq[", tok, 6))
		return w->rq.type == SCHED_RT;

	if (!strncmp("dl_rq[", tok, 6))
		return w->rq.type == SCHED_DL;

	return 0;
}

static int sched_watch_read(struct watch *w)
{
	struct watch_items old_items;
	struct watch_item *item = NULL;
	char *buf, *tok, *tmp = NULL;
	char path[PATH_MAX];
	size_t size;

	scnprintf(path, PATH_MAX, "%s/sched_debug", procfs__mountpoint());

	if (filename__read_str(path, &buf, &size))
		return -1;

	old_items = w->items;
	zero_items(&w->items);

	for_each_token(buf, "\n", tmp) {
		int skip = 0;

		if (is_sched_item(w, tok)) {
			item = new_item(&w->items, rtrim(tok));
			if (!item)
				return -ENOMEM;
		} else if (!strncmp("  .", tok, 3)) {
			if (!item)
				continue;
			if (add_line(&w->items, item, tok, &skip))
				return -ENOMEM;
			if (skip)
				continue;
			if (old_items.item)
				compare_items(&w->items, &old_items);
		} else {
			item = NULL;
		}
	}

	free_items(&old_items);
	free(w->rq.buf);
	w->rq.buf = buf;
	return 0;
}

static char *task_name(struct thread_map_data *m)
{
	static char buf[50];

	scnprintf(buf, 50, "%s-%d", m->comm ? rtrim(m->comm) : "pid", m->pid);
	return buf;
}

static int read_task_sched(struct watch_item *item, int tid,
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

		if (!is_allowed(new_items, name))
			continue;

		line = new_line(item, name, trim(val));
		if (!line)
			return -ENOMEM;

		items_width(new_items, line);

		if (old_items->item)
			compare_items(new_items, old_items);
	}

	free(item->task.buf);
	item->task.buf = buf;
	return 0;
}

static int read_task_status(struct watch_item *item, int tid,
			    struct watch_items *new_items,
			    struct watch_items *old_items)
{
	char *tok, *tmp = NULL;
	char path[PATH_MAX];
	char *buf;
	size_t size;

	scnprintf(path, PATH_MAX, "%s/%d/status", procfs__mountpoint(), tid);

	if (filename__read_str(path, &buf, &size))
		return -1;

	for_each_token(buf, "\n", tmp) {
		struct watch_line *line;
		char *name;
		char *val;

		val = index(tok, ':');
		if (!val)
			continue;

		*val++ = 0x0;
		name = rtrim(tok);

		if (!is_allowed(new_items, name))
			continue;

		line = new_line(item, name, trim(val));
		if (!line)
			return -ENOMEM;

		items_width(new_items, line);

		if (old_items->item)
			compare_items(new_items, old_items);
	}

	free(item->task.buf);
	item->task.buf = buf;
	return 0;
}

typedef int (read_task_fn_t)(struct watch_item*, int,
			     struct watch_items*,
			     struct watch_items*);

static int read_task(struct watch *w, read_task_fn_t fn)
{
	struct thread_map *m = w->task.pid;
	struct watch_items old_items;
	struct watch_item *item;
	int i;

	old_items = w->items;
	zero_items(&w->items);

	for (i = 0; i < m->nr; i++) {
		item = new_item(&w->items, task_name(&m->map[i]));
		if (!item)
			return -ENOMEM;

		if (fn(item, m->map[i].pid, &w->items, &old_items))
			return -EINVAL;
	}

	free_items(&old_items);
	return 0;
}

static int task_status_watch_read(struct watch *w)
{
	return read_task(w, read_task_status);
}

static int task_sched_watch_read(struct watch *w)
{
	return read_task(w, read_task_sched);
}

static struct watch watch[] = {
	{
		.name 		= "rq",
		.read		= sched_watch_read,
		.rq.type	= SCHED_RQ,
		.help		= "CPU runqueues [/proc/sched_debug]",
	},
	{
		.name		= "cfs",
		.read		= sched_watch_read,
		.rq.type	= SCHED_CFS,
		.help		= "CFS groups    [/proc/sched_debug]",
	},
	{
		.name		= "cfs_root",
		.read		= sched_watch_read,
		.rq.type	= SCHED_CFS_ROOT,
		.help		= "CFS roots     [/proc/sched_debug]",
	},
	{
		.name		= "rt",
		.read		= sched_watch_read,
		.rq.type	= SCHED_RT,
		.help		= "RT  runqueues [/proc/sched_debug]",
	},
	{
		.name		= "dl",
		.read		= sched_watch_read,
		.rq.type	= SCHED_DL,
		.help		= "DL  runqueues [/proc/sched_debug]",
	},
	{
		.name		= "sched",
		.read		= task_sched_watch_read,
		.flags		= WATCH_TASK,
		.help		= "task sched    [/proc/pid/sched]",
	},
	{
		.name		= "status",
		.read		= task_status_watch_read,
		.flags		= WATCH_TASK,
		.help		= "task status   [/proc/pid/sched]",
	},
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

static int is_task_watch(struct watch *w)
{
	return w->flags & WATCH_TASK;
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

#define MAX_FIELDS 50
static int setup_fields(struct watch *w, const char *field)
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

	if (!hcreate_r(MAX_FIELDS, &w->items.fields))
		return -1;

	for_each_token(buf, sep, tmp) {
		e.key  = tok;
		e.data = NULL;

		if (!hsearch_r(e, ENTER, &ep, &w->items.fields))
			return -1;
	}

	w->items.has_fields = true;
	return 0;
}

int cmd_watch(int argc, const char **argv,
	      const char *prefix __maybe_unused)
{
	const char *pid = NULL;
	const char *field = NULL;
	const struct option options[] = {
		OPT_INCR('v', "verbose", &verbose,
			 "be more verbose (show counter open errors, etc)"),
		OPT_STRING('p', "pid", &pid, "pid", "pids"),
		OPT_STRING('f', "field", &field, "field", "fields"),
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

	if (pid) {
		if (!is_task_watch(w))
			return -1;

		w->task.pid = thread_map__new_str(pid, NULL, 0);
		if (!w->task.pid)
			return -1;
	} else if (is_task_watch(w)) {
		pr_err("failed: no task specified (use -p option)\n");
		return -1;
	}

	if (field && setup_fields(w, field)) {
		pr_err("failed: initialize fields\n");
		return -1;
	}

	ret = system("clear");
	set_term_quiet_input(&old);

	while (!quit) {
		int c;

		ret = w->read(w);
		if (ret) {
			pr_err("failed: reading data\n");
			return -1;
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
		default:
			break;
		}
	}

	tcsetattr(0, TCSAFLUSH, &old);
	return 0;
}
