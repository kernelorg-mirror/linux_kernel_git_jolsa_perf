#ifndef __PERF_FORMULA
#define __PERF_FORMULA

/*
 * format:
 * set {
 *      events {
 *              CY = cycles
 *              IN = instructions
 *              BR = branches
 *      }
 *
 *      cpi      = CY / IN
 *      bdensity = IN / BR
 *
 *      print cpi
 *      print bdensity
 * }
 *
 * TODO add following syntax:
 *      print cpi = CY / IN
 */

#include <linux/list.h>
#include "evsel.h"
#include "evlist.h"

struct perf_formula {
	struct list_head head_files;
	struct list_head head_sets;
};

struct perf_formula_file {
	char *path;

	struct list_head head_sets;
	struct list_head list;
};

struct perf_formula_set {
	char *name;
	char *events;
	bool  loaded;

	struct list_head head_events;
	struct list_head head_counters;
	struct list_head list;
	/* belongs to perf_formula::head_sets */
	struct list_head list_sets;
};

struct perf_formula_result_value {
	u64    ena;
	u64    run;
	double result;
};

struct perf_formula_result {
	struct perf_formula_result_value aggr;
	struct perf_formula_result_value cpu[];
};

struct perf_formula_event {
	char *name;
	char *config;

	struct perf_evsel *evsel;
	struct list_head list;
};

struct perf_formula_counter {
	char  *name;
	char  *formula;
	bool   print;

	struct perf_formula_set *set;
	struct list_head list;

	struct perf_formula_result *result;
};

struct perf_formula_value {
	char   *name;
	double *ptr;
};

struct perf_formula_expr {
	bool   test_only;
	bool   print;
	bool   system_wide;

	int    error;
	FILE  *file;

	struct perf_evlist         *evlist;
	struct perf_formula_set    *set;
	struct perf_formula_value **values;

	struct perf_formula_result *result;
};

struct perf_formula_ass {
	char *name;
	char *value;
};

struct perf_formula_config {
	enum {
		PERF_FORMULA_CONFIG_EVENTS,
		PERF_FORMULA_CONFIG_PRINT,
		PERF_FORMULA_CONFIG_ASS,
	} type;

	union {
		struct perf_formula_counter *counter;
		struct perf_formula_ass      ass;
		struct list_head             events;
		char                        *print;
	};

	struct list_head list;
};


void perf_formula__init(struct perf_formula *f);
int perf_formula__free(struct perf_formula *f);

int perf_formula__load(struct perf_formula *f, char *path);

struct perf_formula_set*
perf_formula__set(struct perf_formula *f, char *name);

int perf_formula__evlist(struct perf_formula *f,
			 struct perf_formula_set *set,
			 struct perf_evlist *evlist);

struct perf_formula_counter*
perf_formula_counter__new(char *name, struct list_head *head);

struct perf_formula_set*
perf_formula_set__new(char *name, struct list_head *head);

int perf_formula_set__eval(struct perf_formula_set *set,
			   struct perf_evlist *evlist,
			   bool system_wide);

struct perf_formula_result *perf_formula__add(struct perf_formula_expr *expr,
					      struct perf_formula_result *x,
					      struct perf_formula_result *y);

struct perf_formula_result *perf_formula__subtract(struct perf_formula_expr *expr,
						   struct perf_formula_result *x,
						   struct perf_formula_result *y);

struct perf_formula_result *perf_formula__multiple(struct perf_formula_expr *expr,
						   struct perf_formula_result *x,
						   struct perf_formula_result *y);

struct perf_formula_result *perf_formula__divide(struct perf_formula_expr *expr,
						 struct perf_formula_result *x,
						 struct perf_formula_result *y);

struct perf_formula_result *perf_formula__negate(struct perf_formula_expr *expr,
						 struct perf_formula_result *x);

struct perf_formula_result *perf_formula__value(struct perf_formula_expr *expr,
						double x);

struct perf_formula_result* perf_formula_expr__resolve(struct perf_formula_expr *expr,
						       char *name);

void perf_formula__loaded(struct perf_formula *f,
			  struct perf_formula_set *set);

static inline bool perf_formula__is_loaded(struct perf_formula *f)
{
	return !list_empty(&f->head_sets);
}

int perf_formula__print(FILE *file,
			struct perf_formula *f,
			struct perf_evlist *evlist,
			struct perf_formula_value **values,
			bool system_wide);

#endif /* __PERF_FORMULA */
