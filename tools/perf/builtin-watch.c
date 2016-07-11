#include <linux/compiler.h>
#include <linux/types.h>
#include <time.h>
#include <subcmd/parse-options.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <asm/bug.h>
#include <search.h>
#include <api/fs/fs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include "builtin.h"
#include "perf.h"
#include "debug.h"
#include "color.h"
#include "term.h"
#include "string2.h"
#include "thread_map.h"

struct watch;

#define MAX_LINES 100
#define MAX_FIELDS 50

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

	union {
		struct {
			char *buf;
		} task;
	};
};

struct watch_items {
	struct watch_item	*item;
	int			 cnt;
	int			 cnt_iter;

	size_t			 width_name;
	size_t			 width_data;

	bool			 has_fields;
	struct hsearch_data	 fields;

	bool			 has_zeros;
	struct hsearch_data	 zeros;
};

typedef int (*watch_read_fn_t)(struct watch *);

struct plot_line {
	FILE			*file;
	int			 line;
};

struct plot_item {
	FILE			*file;
	int			 item;

	struct plot_line	 line[MAX_LINES];
	int			 cnt;
};

struct watch_plot {
	bool			 enabled;
	char			*file;
	int			 yx;

	struct hsearch_data	 items;
	struct hsearch_data	 fields;

	int			 pi_cnt;
	struct plot_item	 pi[MAX_FIELDS];
};

enum {
	WATCH_TASK = 1 << 0,
};

struct watch {
	const char		*name;
	const char		*help;
	watch_read_fn_t		 read;
	u64			 flags;

	struct watch_items	 items;
	struct watch_plot	 plot;

	union {
		struct {
			int	 type;
			char	*buf;
		} rq;
		struct {
			struct thread_map *pid;
		} task;
		struct {
			char	*buf;
		} interrupt;
	};
};

#define for_each_token(__tok, __buf, __sep, __tmp)		\
	for (__tok = strtok_r(__buf, __sep, &__tmp); __tok;	\
	     __tok = strtok_r(NULL,  __sep, &__tmp))

static int is_allowed(struct watch_items *items, char *name)
{
	ENTRY e, *ep;

	if (!items->has_fields)
		return 1;

	e.key = name;
	return hsearch_r(e, FIND, &ep, &items->fields);
}

static int create_htable(struct hsearch_data *table, size_t size,
			 char *buf, const char *sep)
{
	ENTRY e, *ep;
	char *tok, *tmp = NULL;

	if (!hcreate_r(size, table))
		return -1;

	for_each_token(tok, buf, sep, tmp) {
		e.key  = tok;
		e.data = NULL;

		if (!hsearch_r(e, ENTER, &ep, table))
			return -1;
	}

	return 0;
}

static void plot_line(struct watch_plot *plot,
		      struct watch_item *item,
		      struct watch_line *line)
{
	struct plot_item *pi;
	ENTRY e, *ep;

	e.key = item->name;
	if (!hsearch_r(e, FIND, &ep, &plot->items))
		return;

	pi = ep->data;
	if (!pi) {
		pi = &plot->pi[plot->pi_cnt++];
		ep->data = pi;
		pi->item = item->idx;
	}

	e.key = line->name;
	if (!hsearch_r(e, FIND, &ep, &plot->fields))
		return;

	pi->line[pi->cnt++].line = line->idx;
}

static void items_width(struct watch_items *items,
			struct watch_line *line)
{
	items->width_name = max(items->width_name, strlen(line->name));
	items->width_data = max(items->width_data, strlen(line->data));
}

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

struct zero_item {
	char *data_old;
};

struct zero_field {
	struct zero_item *item;
	int item_cnt;
};

static struct zero_item *zero_item_get(void **ptr, struct watch_item *item)
{
	struct zero_field *field = *ptr;
	struct zero_item *zi;
	int new = 0;

	if (!field) {
		field = zalloc(sizeof(*field));
		if (!field)
			return NULL;

		*ptr = field;
	}

	if (field->item_cnt <= item->idx) {
		zi = realloc(field->item, sizeof(*zi) * (item->idx + 1));
		if (!zi)
			return NULL;

		field->item     = zi;
		field->item_cnt = item->idx + 1;

		new = 1;
	}

	zi = &field->item[item->idx];

	if (new)
		memset(zi, 0x0, sizeof(*zi));

	return zi;
}

static int zero_line(struct watch_item *item, struct watch_line *line,
		     char **data_new, int *free_data)
{
	struct watch *w = item->watch;
	struct zero_item *zi;
	ENTRY e, *ep;

	e.key = line->name;
	if (!hsearch_r(e, FIND, &ep, &w->items.zeros))
		return 0;

	zi = zero_item_get(&ep->data, item);
	if (!zi)
		return -ENOMEM;

	if (zi->data_old) {
		unsigned long val_new, val_old;
		char *endptr;

		val_old = strtoull(zi->data_old,  &endptr, 0);
		val_new = strtoull(*data_new, &endptr, 0);

		zi->data_old = *data_new;

		if (asprintf(data_new, "%lu", val_new - val_old) < 0)
			return -ENOMEM;

		*free_data   = 1;
	} else {
		zi->data_old = *data_new;
		*data_new    = strdup("0");
	}

	return *data_new ? 0 : -1;
}

#define MAX_DATALEN 30

static struct watch_line* new_line(struct watch_item *item,
				   char *name, char *data,
				   int *skip)
{
	struct watch *w = item->watch;
	struct watch_line *line;
	int free_data = 0;

	if (item->cnt_iter == MAX_LINES)
		return NULL;

	if (!is_allowed(&w->items, name)) {
		*skip = 1;
		return NULL;
	}

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

		if (w->items.has_zeros && zero_line(item, line, &data, &free_data))
			return NULL;

		if (w->plot.enabled)
			plot_line(&w->plot, item, line);

	} else {
		/* Old line. */
		int equal = 0;

		/* Line possition/name changed, fail for now. */
		if (strcmp(line->name, name))
			return NULL;

		if (w->items.has_zeros && zero_line(item, line, &data, &free_data))
			return NULL;

		if (!strcmp(line->data, data))
			equal = 1;

		if (equal && line->color)
			line->color -= 1;

		if (!equal)
			line->color = 3;
	}

	if (free_data)
		free(line->data);

	line->data = data;

	item->cnt_iter++;
	return line;
}

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

enum {
	SCHED_RQ,
	SCHED_CFS,
	SCHED_CFS_ROOT,
	SCHED_RT,
	SCHED_DL,
};

static int add_sched_line(struct watch_items *items,
			  struct watch_item *item, char *str,
			  int *skip)
{
	struct watch_line *line;
	char *name, *data;

	data = index(str, ':');
	*data++ = 0x0;
	name = trim(str);

	line = new_line(item, name, data, skip);
	if (!line)
		return -ENOMEM;

	items_width(items, line);
	return 0;
}

static struct watch_item *add_sched_item(struct watch *w, char *name)
{
	if (w->rq.type == SCHED_RQ) {
		/*
		 * Change 'cpu#0, 2594.054 MHz' into 'cpu#0'.
		 */
		char *c = strchr(name, ',');

		if (c)
			*c = 0;
	}

	return new_item(w, name);
}

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
	struct watch_item *item = NULL;
	char *buf, *tok, *tmp = NULL;
	char path[PATH_MAX];
	size_t size;

	scnprintf(path, PATH_MAX, "%s/sched_debug", procfs__mountpoint());

	if (filename__read_str(path, &buf, &size))
		return -1;

	zero_items(&w->items);

	for_each_token(tok, buf, "\n", tmp) {
		int skip = 0;

		if (is_sched_item(w, tok)) {
			item = add_sched_item(w, rtrim(tok));
			if (!item)
				return -ENOMEM;
		} else if (!strncmp("  .", tok, 3)) {
			if (!item)
				continue;
			if (add_sched_line(&w->items, item, tok, &skip)) {
				if (skip)
					continue;
				return -ENOMEM;
			}
		} else {
			item = NULL;
		}
	}

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
			   struct watch_items *items)
{
	char *tok, *tmp = NULL;
	char path[PATH_MAX];
	char *buf;
	size_t size;
	int start = 0;

	scnprintf(path, PATH_MAX, "%s/%d/sched", procfs__mountpoint(), tid);

	if (filename__read_str(path, &buf, &size))
		return -1;

	for_each_token(tok, buf, "\n", tmp) {
		struct watch_line *line;
		char *name;
		char *val;
		int skip = 0;

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

		line = new_line(item, name, trim(val), &skip);
		if (!line) {
			if (skip)
				continue;
			return -ENOMEM;
		}

		items_width(items, line);
	}

	free(item->task.buf);
	item->task.buf = buf;
	return 0;
}

static int read_task_status(struct watch_item *item, int tid,
			    struct watch_items *items)
{
	char *tok, *tmp = NULL;
	char path[PATH_MAX];
	char *buf;
	size_t size;

	scnprintf(path, PATH_MAX, "%s/%d/status", procfs__mountpoint(), tid);

	if (filename__read_str(path, &buf, &size))
		return -1;

	for_each_token(tok, buf, "\n", tmp) {
		struct watch_line *line;
		char *name;
		char *val;
		int skip = 0;

		val = index(tok, ':');
		if (!val)
			continue;

		*val++ = 0x0;
		name = rtrim(tok);

		line = new_line(item, name, trim(val), &skip);
		if (!line) {
			if (skip)
				continue;
			return -ENOMEM;
		}

		items_width(items, line);
	}

	free(item->task.buf);
	item->task.buf = buf;
	return 0;
}


typedef int (read_task_fn_t)(struct watch_item*, int,
			     struct watch_items*);

static int read_task(struct watch *w, read_task_fn_t fn)
{
	struct thread_map *m = w->task.pid;
	struct watch_item *item;
	int i;

	zero_items(&w->items);

	for (i = 0; i < m->nr; i++) {
		item = new_item(w, task_name(&m->map[i]));
		if (!item)
			return -ENOMEM;

		if (fn(item, m->map[i].pid, &w->items))
			return -EINVAL;
	}

	return 0;
}

static int task_sched_watch_read(struct watch *w)
{
	return read_task(w, read_task_sched);
}

static int task_status_watch_read(struct watch *w)
{
	return read_task(w, read_task_status);
}

static int interrupts(struct watch *w)
{
	struct watch_item *item = NULL;
	char *buf, *tok, *tmp = NULL;
	char path[PATH_MAX];
	size_t size;
	int first_line = 1;
	int count = 0;
	char **val = NULL;

	scnprintf(path, PATH_MAX, "%s/interrupts", procfs__mountpoint());

	if (filename__read_str(path, &buf, &size))
		return -1;

	buf[size] = 0x0;

	zero_items(&w->items);

	for_each_token(tok, buf, "\n", tmp) {
		int i = 0, first_col = 1;
		char *name = NULL;
		char *tok1, *tmp1 = NULL;

		if (first_line) {
			for_each_token(tok1, tok, " ", tmp1) {
				item = new_item(w, rtrim(tok1));
				if (!item)
					return -ENOMEM;
				count++;
			}

			first_line = 0;
			continue;
		}

		if (!val) {
			val = zalloc(sizeof(*val) * count);
			if (!val)
				return -ENOMEM;
		}

		memset(val, 0x0, sizeof(*val) * count);

		for_each_token(tok1, tok, " ", tmp1) {
			if (first_col) {
				name = trim(tok1);
				first_col = 0;
				continue;
			}

			if (i < count) {
				val[i] = trim(tok1);
				i++;
				continue;
			}
			break;
		}

		/* get rid of ':' */
		name[strlen(name) - 1] = 0x0;

		for (i = 0; i < count; i++) {
			struct watch_line *line;
			int skip = 0;

			if (!val[i])
				val[i] = (char *) "";

			line = new_line(&w->items.item[i], name, val[i], &skip);
			if (!line) {
				if (skip)
					break;

				return -ENOMEM;
			}

			items_width(&w->items, line);
		}
	}

	free(val);
	free(w->interrupt.buf);
	w->interrupt.buf = buf;
	return 0;
}

static struct watch watch[] = {
	{
		.name 		= "rq",
		.read		= sched_watch_read,
		.rq		= {
			.type	= SCHED_RQ,
		},
		.help		= "CPU runqueues [/proc/sched_debug]",
	},
	{
		.name		= "cfs",
		.read		= sched_watch_read,
		.rq		= {
			.type	= SCHED_CFS,
		},
		.help		= "CFS groups    [/proc/sched_debug]",
	},
	{
		.name		= "cfs_root",
		.read		= sched_watch_read,
		.rq		= {
			.type	= SCHED_CFS_ROOT,
		},
		.help		= "CFS roots     [/proc/sched_debug]",
	},
	{
		.name		= "rt",
		.read		= sched_watch_read,
		.rq		= {
			.type	= SCHED_RT,
		},
		.help		= "RT  runqueues [/proc/sched_debug]",
	},
	{
		.name		= "dl",
		.read		= sched_watch_read,
		.rq		= {
			.type	= SCHED_DL,
		},
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
		.help		= "task status   [/proc/pid/status]",
	},
	{
		.name		= "int",
		.read		= interrupts,
		.help		= "interrupts    [/proc/interrupts]",
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

static int setup_fields(struct watch *w, const char *field)
{
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

	if (create_htable(&w->items.fields, MAX_FIELDS, buf, sep))
		return -1;

	w->items.has_fields = true;
	return 0;
}

static int setup_plot(struct watch *w, const char *str_)
{
	struct watch_plot *plot = &w->plot;
	char *tok, *tmp = NULL;
	char *str    = strdup(str_);
	char *items  = NULL;
	char *fields = NULL;
	char *file   = NULL;
	char *yx     = NULL;

	for_each_token(tok, str, ":", tmp) {
		if (!items)
			items = tok;
		else if (!fields)
			fields = tok;
		else if (!yx) {
			int yxb;

			yx  = tok;
			yxb = !strcmp(yx, "yx");

			if (!yxb && strcmp(yx, "xy"))
				file = tok;
			else
				plot->yx = yxb;
		} else if (!file) {
			file = tok;
		} else {
			return -1;
		}
	}

	if (file)
		plot->file = strdup(file);
	else
		plot->file = strdup("plot.data");

	if (create_htable(&plot->items, MAX_FIELDS, items, ","))
		return -1;

	if (create_htable(&plot->fields, MAX_FIELDS, fields, ","))
		return -1;

	plot->enabled = true;
	return 0;
}

static int plot_watch_xy(struct watch *w)
{
	struct watch_line *line;
	struct plot_item *pi;
	int i;

	for (i = 0; i < w->plot.pi_cnt; i++) {
		struct watch_item *item;
		int l;

		pi = &w->plot.pi[i];
		item = &w->items.item[pi->item];

		if (!pi->file) {
			char path[PATH_MAX];

			scnprintf(path, PATH_MAX, "%s-%s", w->plot.file, item->name);
			pi->file = fopen(path, "w+");
			if (!pi->file)
				return -EINVAL;

			fprintf(pi->file, "# time ");

			for (l = 0; l < pi->cnt; l++) {
				line = &item->line[pi->line[l].line];
				fprintf(pi->file, "%s ", line->name);
			}

			fprintf(pi->file, "\n");
		}

		fprintf(pi->file, "%u ", (unsigned int) time(NULL));

		for (l = 0; l < pi->cnt; l++) {
			line = &item->line[pi->line[l].line];
			fprintf(pi->file, "%s ", line->data);
		}

		fprintf(pi->file, "\n");

		fflush(pi->file);
	}

	return 0;
}

static int plot_watch_yx(struct watch *w)
{
	struct watch_item *item;
	struct watch_item *item0;
	struct plot_item *pi0 = &w->plot.pi[0];
	struct plot_item *pi;
	struct plot_line *pl;
	int i, l;

	item0 = &w->items.item[pi0->item];

	for (l = 0; l < pi0->cnt; l++) {
		pl = &pi0->line[l];

		if (!pl->file) {
			char path[PATH_MAX];

			scnprintf(path, PATH_MAX, "%s-%s", w->plot.file, item0->line[pl->line].name);
			pl->file = fopen(path, "w+");
			if (!pl->file)
				return -EINVAL;

			fprintf(pl->file, "# time ");

			for (i = 0; i < w->plot.pi_cnt; i++) {
				pi = &w->plot.pi[i];
				item = &w->items.item[pi->item];
				fprintf(pl->file, "%s ", item->name);
			}

			fprintf(pl->file, "\n");
		}

		fprintf(pl->file, "%u ", (unsigned int) time(NULL));

		for (i = 0; i < w->plot.pi_cnt; i++) {
			pi = &w->plot.pi[i];
			item = &w->items.item[pi->item];
			fprintf(pl->file, "%s ", item->line[pi->line[l].line].data);
		}

		fprintf(pl->file, "\n");
		fflush(pl->file);
	}

	return 0;
}

static int plot_watch(struct watch *w)
{
	return w->plot.yx ? plot_watch_yx(w) : plot_watch_xy(w);
}

static int setup_zero(struct watch *w, const char *str_)
{
	char *str = strdup(str_);

	if (create_htable(&w->items.zeros, MAX_FIELDS, str, ","))
		return -1;

	w->items.has_zeros = true;
	return 0;
}

int cmd_watch(int argc __maybe_unused, const char **argv __maybe_unused)
{
	const char *field = NULL;
	const char *plot = NULL;
	const char *zero = NULL;
	const char *pid = NULL;
	const struct option options[] = {
		OPT_INCR('v', "verbose", &verbose,
			 "be more verbose (show counter open errors, etc)"),
		OPT_STRING('f', "field", &field, "field", "fields"),
		OPT_STRING(0, "plot", &plot, "field", "fields"),
		OPT_STRING(0, "zero", &zero, "field", "fields"),
		OPT_STRING('p', "pid", &pid, "pid", "pids"),
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

	if (field && setup_fields(w, field)) {
		pr_err("failed: initialize fields\n");
		return -1;
	}

	if (plot && setup_plot(w, plot)) {
		pr_err("failed: initialize plots\n");
		return -1;
	}

	if (zero && setup_zero(w, zero)) {
		pr_err("failed: initialize zeros\n");
		return -1;
	}

	if (pid) {
		if (!is_task_watch(w))
			return -1;

		w->task.pid = thread_map__new_str(pid, NULL, 0, false);
		if (!w->task.pid)
			return -1;
	} else if (is_task_watch(w)) {
		pr_err("failed: no task specified (use -p option)\n");
		return -1;
	}

	ret = system("clear");
	set_term_quiet_input(&old);

	while (!quit) {
		int c;

		ret = w->read(w);
		if (ret) {
			pr_err("failed: reading data\n");
			break;
		}

		if (w->plot.enabled) {
			ret = plot_watch(w);
			if (ret) {
				pr_err("failed: plotting data\n");
				break;
			}
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
