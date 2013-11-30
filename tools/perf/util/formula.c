
#include <linux/compiler.h>
#include <stdio.h>
#include "stat.h"
#include "parse-events.h"
#include "formula.h"
#include "formula-bison.h"
#define YY_EXTRA_TYPE int
#include "formula-flex.h"
#include "cpumap.h"

#define FORMULA_SET_ALL ((void *) -1)

#ifdef PARSER_DEBUG
extern int perf_formula_debug;
#endif

void perf_formula__init(struct perf_formula *f)
{
	memset(f, 0x0, sizeof(*f));
	INIT_LIST_HEAD(&f->head_files);
	INIT_LIST_HEAD(&f->head_sets);
}

static int scanner_expr(const char *str, void *data)
{
	YY_BUFFER_STATE buffer;
	void *scanner;
	int ret;

	ret = perf_formula_lex_init_extra(PF_START_EXPR, &scanner);
	if (ret)
		return ret;

	buffer = perf_formula__scan_string(str, scanner);

#ifdef PARSER_DEBUG
	perf_formula_debug = 1;
#endif
	ret = perf_formula_parse(data, scanner);

	perf_formula__flush_buffer(buffer, scanner);
	perf_formula__delete_buffer(buffer, scanner);
	perf_formula_lex_destroy(scanner);
	return ret;
}

static int scanner_config(FILE *file, void *data)
{
	void *scanner;
	int ret;

	ret = perf_formula_lex_init_extra(PF_START_CONFIG, &scanner);
	if (ret)
		return ret;

	perf_formula_set_in(file, scanner);

#ifdef PARSER_DEBUG
	perf_formula_debug = 1;
#endif
	ret = perf_formula_parse(data, scanner);

	perf_formula_lex_destroy(scanner);
	return ret;
}

static int cpu_nr(struct perf_formula_expr *expr)
{
	return cpu_map__nr(expr->evlist->cpus);
}

static struct perf_formula_result*
alloc_result(struct perf_formula_expr *expr)
{
	int nr = 0;

	if (expr->system_wide)
		nr = cpu_nr(expr);

	return zalloc((sizeof(struct perf_formula_result) +
		      (nr * sizeof(struct perf_formula_result))));
}

static int config_parse(struct perf_formula_file *file)
{
	FILE *f;
	int ret;

	f = fopen(file->path, "r");
	if (!f)
		return -EINVAL;

	ret = scanner_config(f, file);

	fclose(f);
	return ret;
}

static int counter_init(struct perf_formula_counter *counter)
{
	struct perf_formula_expr expr = {
		.test_only = true,
	};

	return scanner_expr(counter->formula, &expr);
}

static int set_init(struct perf_formula_set *set)
{
	struct perf_formula_counter *counter;
	int ret = 0;

	list_for_each_entry(counter, &set->head_counters, list) {
		ret = counter_init(counter);
		if (ret)
			break;

		counter->set = set;
	}

	return ret;
}

static int file_init(struct perf_formula_file *file)
{
	struct perf_formula_set *set;
	int ret;

	ret = config_parse(file);

	list_for_each_entry(set, &file->head_sets, list) {
		ret = set_init(set);
		if (ret)
			break;
	}

	return ret;
}

static void file_free(struct perf_formula_file *file)
{
	struct perf_formula_set *set;

	list_for_each_entry(set, &file->head_sets, list) {
		struct perf_formula_counter *counter;

		list_for_each_entry(counter, &set->head_counters, list) {
			free(counter->result);
			free(counter);
		}

		free(set);
	}

	free(file->path);
	free(file);
}

int perf_formula__load(struct perf_formula *f, char *path)
{
	struct perf_formula_file *file;
	int ret;

	file = zalloc(sizeof(*file));
	if (!file)
		return -ENOMEM;

	INIT_LIST_HEAD(&file->list);
	INIT_LIST_HEAD(&file->head_sets);
	file->path = strdup(path);

	ret = file_init(file);
	if (ret)
		file_free(file);
	else
		list_add_tail(&file->list, &f->head_files);

	return ret;
}

int perf_formula__free(struct perf_formula *f)
{
	struct perf_formula_file *file;

	list_for_each_entry(file, &f->head_files, list)
		file_free(file);

	return 0;
}

enum {
	CB_NEXT,
	CB_OK,
	CB_FAIL,
};

typedef int (*set_cb)(struct perf_formula_set *set, void *data);

static int for_each_set(struct perf_formula *formula,
			set_cb cb, void *data)
{
	struct perf_formula_file *file;

	list_for_each_entry(file, &formula->head_files, list) {
		struct perf_formula_set *set;

		list_for_each_entry(set, &file->head_sets, list) {
			int ret = cb(set, data);

			if (ret == CB_NEXT)
				continue;
			else if (ret == CB_OK)
				return 0;
			else if (ret == CB_FAIL)
				return -1;
		}
	}

	return 0;
}

struct find_set_data {
	struct perf_formula_set *set;
	char *name;
};

static int find_set_cb(struct perf_formula_set *set, void *data)
{
	struct find_set_data *d = data;

	if (strcmp(set->name, d->name))
		return CB_NEXT;

	d->set = set;
	return CB_OK;
}

struct perf_formula_set*
perf_formula__set(struct perf_formula *f, char *name)
{
	struct find_set_data data = {
		.name = name,
	};

	if (!strcmp(name, "all"))
		return FORMULA_SET_ALL;

	if (for_each_set(f, find_set_cb, &data))
		return NULL;

	return data.set;
}

static int resolve_events(struct perf_formula_set *set,
			  struct perf_evlist *evlist,
			  struct perf_evsel *evsel)
{
	struct perf_formula_event *event;

	list_for_each_entry(event, &set->head_events, list) {
		list_for_each_entry_continue(evsel, &evlist->entries, node) {
			if (!strcmp(event->config, evsel->name)) {
				event->evsel = evsel;
				break;
			}
		}
		if (!event->evsel) {
			pr_err("formula: no evenst match for %s\n",
			       event->config);
			return -1;
		}
	}

	return 0;
}

static int set_evlist(struct perf_formula_set *set,
		      struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	if (set->loaded)
		return 0;

	evsel = list_entry(evlist->entries.prev, struct perf_evsel, node);

	if (parse_events(evlist, set->events))
		return -1;

	if (resolve_events(set, evlist, evsel))
		return -1;

	set->loaded = true;
	return 0;
}

static int evlist_cb(struct perf_formula_set *set, void *data)
{
       struct perf_evlist *evlist = data;

	if (set_evlist(set, evlist))
		return CB_FAIL;

	return CB_NEXT;
}

int perf_formula__evlist(struct perf_formula *f,
			 struct perf_formula_set *set,
			 struct perf_evlist *evlist)
{
	if (set != FORMULA_SET_ALL)
		return set_evlist(set, evlist);

	return for_each_set(f, evlist_cb, (void *) evlist);
}

static struct perf_formula_counter*
set_counter(struct perf_formula_set *set, struct perf_formula_config *config)
{
	struct perf_formula_counter *counter;

	counter = zalloc(sizeof(*counter));
	if (!counter)
		return NULL;

	INIT_LIST_HEAD(&counter->list);
	counter->name    = config->ass.name;
	counter->formula = config->ass.value;
	counter->set     = set;
	counter->print   = false;

	list_add_tail(&counter->list, &set->head_counters);
	return counter;
}

static int
set_events(struct perf_formula_set *set, struct perf_formula_config *config)
{
	struct perf_formula_config *event;
#define MAX_EVENTS 4096
	char buf[MAX_EVENTS];

	/* grouped by default */
	buf[0] = '{';
	buf[1] = 0x0;

	list_for_each_entry(event, &config->events, list) {
		struct perf_formula_event *e;

		e = zalloc(sizeof(*e));
		if (!e)
			goto out;

		INIT_LIST_HEAD(&e->list);
		e->name   = event->ass.name;
		e->config = event->ass.value;
		list_add_tail(&e->list, &set->head_events);

		/* TODO length check */
		strcat(buf, event->ass.value);

		if (!list_is_last(&event->list, &config->events))
			strcat(buf, ",");
	}

	strcat(buf, "}");
	set->events = strdup(buf);
	return 0;

 out:
	/* TODO free everything ;-) */
	return -ENOMEM;
}

static int
set_print(struct perf_formula_set *set, struct perf_formula_config *config)
{
	struct perf_formula_counter *counter;

	list_for_each_entry(counter, &set->head_counters, list) {
		if (!strcmp(counter->name, config->print)) {
			counter->print = true;
			return 0;
		}
	}

	return -1;
}

struct perf_formula_set*
perf_formula_set__new(char *name, struct list_head *head)
{
	struct perf_formula_set *set;
	struct perf_formula_config *config;

	set = zalloc(sizeof(*set));
	if (!set)
		return NULL;

	INIT_LIST_HEAD(&set->list);
	INIT_LIST_HEAD(&set->head_counters);
	INIT_LIST_HEAD(&set->head_events);

	list_for_each_entry(config, head, list) {
		switch (config->type) {
		case PERF_FORMULA_CONFIG_EVENTS:
			if (set_events(set, config))
				goto out;
			break;

		case PERF_FORMULA_CONFIG_PRINT:
			if (set_print(set, config))
				goto out;
			break;

		case PERF_FORMULA_CONFIG_ASS:
			if (!set_counter(set, config))
				goto out;
			break;

		default:
			BUG_ON(1);
		}
	}

	set->name = strdup(name);
	return set;

 out:
	free(set);
	return NULL;
}

static int eval_counter(struct perf_formula_counter *counter,
			struct perf_formula_expr *expr)
{
	int ret;

	pr_debug2("formula: Processing formula %s\n", counter->formula);

	ret = scanner_expr(counter->formula, expr);

	if (expr->error) {
		pr_err("formula: failed to evaluate expression with %d\n",
			expr->error);
		return expr->error;
	}

	if (!ret) {
		counter->result = expr->result;
		pr_debug2("formula counter eval %s\n", counter->name);
	}

	return ret;
}

static int eval_set_cb(struct perf_formula_set *set, void *data)
{
	struct perf_formula_expr *expr = data;
	struct perf_formula_counter *counter;

	expr->set = set;

	pr_debug2("formula eval %s\n", set->name);

	list_for_each_entry(counter, &set->head_counters, list) {
		if (eval_counter(counter, expr)) {
			pr_err("failed to eval counter %s\n",
			        counter->name);
			return CB_FAIL;
		}
		if (expr->print && counter->print)
			fprintf(expr->file,
				"%'18.8F %-25s\n",
				expr->result->aggr.result, counter->name);

	}

	return CB_NEXT;
}

int perf_formula__eval(struct perf_formula *f,
		       struct perf_formula_set *set,
		       struct perf_formula_expr *expr)
{
	if (set != FORMULA_SET_ALL)
		return eval_set_cb(set, expr);

	return for_each_set(f, eval_set_cb, expr);
}

static struct perf_formula_event*
resolve_event(struct perf_formula_expr *expr, char *name)
{
	struct perf_formula_set *set = expr->set;
	struct perf_formula_event *event;

	list_for_each_entry(event, &set->head_events, list) {
		if (strcmp(event->name, name))
			continue;

		return event;
	}

	return NULL;
}

static struct perf_formula_counter*
resolve_counter(struct perf_formula_expr *expr, char *name)
{
	struct perf_formula_set *set = expr->set;
	struct perf_formula_counter *counter;

	list_for_each_entry(counter, &set->head_counters, list) {
		if (!strcmp(counter->name, name))
			return counter;
	}

	return NULL;
}

struct perf_formula_result*
perf_formula_expr__resolve(struct perf_formula_expr *expr,
			   char *name)
{
	struct perf_formula_event *evsel;
	struct perf_formula_counter *counter;
	struct perf_formula_result *result;

	if (expr->test_only) {
		expr->error = 1;
		return NULL;
	}

	if (expr->error != 0)
		return NULL;

	evsel = resolve_event(expr, name);
	if (evsel) {
		struct perf_counts *ecounts = evsel->evsel->counts;

		pr_debug2("formula resolve: (event) %s\n", name);

		if (evsel->result)
			/* data already copied over, just return pointer */
			return evsel->result;

		result = alloc_result(expr);
		if (!result) {
			expr->error = -ENOMEM;
			return NULL;
		}

		/* copy data over */
		if (!expr->system_wide) {
			result->aggr.result = ecounts->aggr.val;
			result->aggr.ena = ecounts->aggr.ena;
			result->aggr.run = ecounts->aggr.run;
			pr_debug2("formula resolve: (event) %s = %F\n", name, result->aggr.result);
		} else {
			int cpu;

			pr_debug2("nr_cpus = %d\n", cpu_nr(expr));
			for (cpu=0; cpu < cpu_nr(expr); cpu++) {
				result->cpu[cpu].result = ecounts->cpu[cpu].val;
				result->cpu[cpu].ena = ecounts->cpu[cpu].ena;
				result->cpu[cpu].run = ecounts->cpu[cpu].run;
				pr_debug2("formula resolve: (event, sw)[%d] %s = %F\n", cpu, name, result->cpu[cpu].result);
			}
		}

		evsel->result = result;
		return result;
	}

	counter = resolve_counter(expr, name);
	if (counter) {
		pr_debug2("formula resolve: (counter) %s\n", name);

		/* should have already been pre-calculated before this formula */
		if (!counter->result) {
			pr_err("formula: resolved to uninitialized counter '%s'\n",
				name);
			expr->error = -EINVAL;
			return NULL;
		}

		return counter->result;
	}

	pr_err("formula: failed to resolve '%s'\n", name);
	expr->error = -EINVAL;
	return NULL;

}

static int check_expr_errors(struct perf_formula_expr *expr,
			     struct perf_formula_result *x,
			     struct perf_formula_result *y)
{
	if (!x || !y) {
		if (!expr->error) {
			/* NULL pointer should have an error associated with it */
			pr_err("formula: NULL expression with no error, set %s\n",
				expr->set->name);
			expr->error = -EINVAL;
		}
		/* error resolving formulas, pass it up */
		return 1;
	}
	return 0;
}

struct perf_formula_result* perf_formula__add(struct perf_formula_expr *expr,
					      struct perf_formula_result *x,
					      struct perf_formula_result *y)
{
	struct perf_formula_result *result;

	if (check_expr_errors(expr, x, y))
		return NULL;

	result = alloc_result(expr);
	if (!result) {
		expr->error = -ENOMEM;
		return NULL;
	}

	if (!expr->system_wide) {
		result->aggr.result = x->aggr.result + y->aggr.result;
		result->aggr.ena = x->aggr.ena + y->aggr.ena;
		result->aggr.run = x->aggr.run + y->aggr.run;
	} else {
		int i;

		for (i=0; i < cpu_nr(expr); i++) {
			result->cpu[i].result = x->cpu[i].result + y->cpu[i].result;
			result->cpu[i].ena = x->cpu[i].ena + y->cpu[i].ena;
			result->cpu[i].run = x->cpu[i].run + y->cpu[i].run;
		}
	}

	return result;
}

struct perf_formula_result *perf_formula__subtract(struct perf_formula_expr *expr,
						   struct perf_formula_result *x,
						   struct perf_formula_result *y)
{
	struct perf_formula_result *result;

	if (check_expr_errors(expr, x, y))
		return NULL;

	result = alloc_result(expr);
	if (!result) {
		expr->error = -ENOMEM;
		return NULL;
	}

	if (!expr->system_wide) {
		result->aggr.result = x->aggr.result - y->aggr.result;
		result->aggr.ena = x->aggr.ena + y->aggr.ena;
		result->aggr.run = x->aggr.run + y->aggr.run;
	} else {
		int i;

		for (i=0; i < cpu_nr(expr); i++) {
			result->cpu[i].result = x->cpu[i].result - y->cpu[i].result;
			result->cpu[i].ena = x->cpu[i].ena + y->cpu[i].ena;
			result->cpu[i].run = x->cpu[i].run + y->cpu[i].run;
		}
	}

	return result;
}

struct perf_formula_result* perf_formula__multiple(struct perf_formula_expr *expr,
						   struct perf_formula_result *x,
						   struct perf_formula_result *y)
{
	struct perf_formula_result *result;

	if (check_expr_errors(expr, x, y))
		return NULL;

	result = alloc_result(expr);
	if (!result) {
		expr->error = -ENOMEM;
		return NULL;
	}

	if (!expr->system_wide) {
		result->aggr.result = x->aggr.result * y->aggr.result;
		result->aggr.ena = x->aggr.ena + y->aggr.ena;
		result->aggr.run = x->aggr.run + y->aggr.run;
	} else {
		int i;

		for (i=0; i < cpu_nr(expr); i++) {
			result->cpu[i].result = x->cpu[i].result * y->cpu[i].result;
			result->cpu[i].ena = x->cpu[i].ena + y->cpu[i].ena;
			result->cpu[i].run = x->cpu[i].run + y->cpu[i].run;
		}
	}

	return result;
}

struct perf_formula_result *perf_formula__divide(struct perf_formula_expr *expr,
						 struct perf_formula_result *x,
						 struct perf_formula_result *y)
{

	struct perf_formula_result *result;

	if (check_expr_errors(expr, x, y))
		return NULL;

	result = alloc_result(expr);
	if (!result) {
		expr->error = -ENOMEM;
		return NULL;
	}

	if (!expr->system_wide) {
		/* check for divide by zero */
		if (!y->aggr.result) {
			pr_err("formula: (aggr) divide by zero error\n");
			result->aggr.result = 0.0;
		} else {
			result->aggr.result = x->aggr.result / y->aggr.result;
		}

		result->aggr.ena = x->aggr.ena + y->aggr.ena;
		result->aggr.run = x->aggr.run + y->aggr.run;
	} else {
		int i;

		for (i=0; i < cpu_nr(expr); i++) {
			/* check for divide by zero */
			if (!y->cpu[i].result) {
				pr_err("formula: (system-wide) divide by zero error on cpu(%d)\n", i);
				result->cpu[i].result = 0.0;
			} else {
				result->cpu[i].result = x->cpu[i].result / y->cpu[i].result;
			}
			result->cpu[i].ena = x->cpu[i].ena + y->cpu[i].ena;
			result->cpu[i].run = x->cpu[i].run + y->cpu[i].run;
		}
	}

	return result;
}

struct perf_formula_result *perf_formula__negate(struct perf_formula_expr *expr,
						 struct perf_formula_result *x)
{
	struct perf_formula_result *result;

	if (check_expr_errors(expr, x, x))
		return NULL;

	result = alloc_result(expr);
	if (!result) {
		expr->error = -ENOMEM;
		return NULL;
	}

	if (!expr->system_wide) {
		result->aggr.result =  -(x->aggr.result);
		result->aggr.ena = x->aggr.ena;
		result->aggr.run = x->aggr.run;
	} else {
		int i;

		for (i=0; i < cpu_nr(expr); i++) {
			result->cpu[i].result = -(x->cpu[i].result);
			result->cpu[i].ena = x->cpu[i].ena;
			result->cpu[i].run = x->cpu[i].run;
		}
	}

	return result;
}

struct perf_formula_result *perf_formula__value(struct perf_formula_expr *expr,
						double x)
{
	struct perf_formula_result *result;

	if (expr->test_only)
		return NULL;

	result = alloc_result(expr);
	if (!result) {
		expr->error = -ENOMEM;
		return NULL;
	}

	if (!expr->system_wide) {
		result->aggr.result = x;
		result->aggr.ena = 1;
		result->aggr.run = 1;
	} else {
		int i;

		for (i=0; i < cpu_nr(expr); i++) {
			result->cpu[i].result = x;
			result->cpu[i].ena = 1;
			result->cpu[i].run = 1;
		}
	}

	return result;
}

void perf_formula__loaded(struct perf_formula *f,
			  struct perf_formula_set *set)
{
	if (list_empty(&set->list_sets))
		list_add_tail(&set->list_sets, &f->head_sets);
}

int perf_formula__print(FILE *file,
			struct perf_formula *f,
			struct perf_evlist *evlist,
			struct perf_formula_value **values)
{
	struct perf_formula_set *set;
	struct perf_formula_expr expr = {
		.evlist = evlist,
		.values = values,
		.print = true,
		.file = file,
	};

	list_for_each_entry(set, &f->head_sets, list)
		eval_set_cb(set, &expr);

	return 0;
}
