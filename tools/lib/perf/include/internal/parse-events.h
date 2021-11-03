/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_PARSE_EVENTS_H
#define __LIBPERF_PARSE_EVENTS_H

#include <linux/types.h>
#include <linux/list.h>
#include <unistd.h>
#include <linux/perf_event.h>

struct event_symbol {
	const char	*symbol;
	const char	*alias;
};
extern struct event_symbol event_symbols_hw[];
extern struct event_symbol event_symbols_sw[];
extern const char *config_term_names[];

enum {
	PARSE_EVENTS__TERM_TYPE_NUM,
	PARSE_EVENTS__TERM_TYPE_STR,
};

enum {
	PARSE_EVENTS__TERM_TYPE_USER,
	PARSE_EVENTS__TERM_TYPE_CONFIG,
	PARSE_EVENTS__TERM_TYPE_CONFIG1,
	PARSE_EVENTS__TERM_TYPE_CONFIG2,
	PARSE_EVENTS__TERM_TYPE_NAME,
	PARSE_EVENTS__TERM_TYPE_SAMPLE_PERIOD,
	PARSE_EVENTS__TERM_TYPE_SAMPLE_FREQ,
	PARSE_EVENTS__TERM_TYPE_BRANCH_SAMPLE_TYPE,
	PARSE_EVENTS__TERM_TYPE_TIME,
	PARSE_EVENTS__TERM_TYPE_CALLGRAPH,
	PARSE_EVENTS__TERM_TYPE_STACKSIZE,
	PARSE_EVENTS__TERM_TYPE_NOINHERIT,
	PARSE_EVENTS__TERM_TYPE_INHERIT,
	PARSE_EVENTS__TERM_TYPE_MAX_STACK,
	PARSE_EVENTS__TERM_TYPE_MAX_EVENTS,
	PARSE_EVENTS__TERM_TYPE_NOOVERWRITE,
	PARSE_EVENTS__TERM_TYPE_OVERWRITE,
	PARSE_EVENTS__TERM_TYPE_DRV_CFG,
	PARSE_EVENTS__TERM_TYPE_PERCORE,
	PARSE_EVENTS__TERM_TYPE_AUX_OUTPUT,
	PARSE_EVENTS__TERM_TYPE_AUX_SAMPLE_SIZE,
	PARSE_EVENTS__TERM_TYPE_METRIC_ID,
	__PARSE_EVENTS__TERM_TYPE_NR,
};

struct parse_events_array {
	size_t nr_ranges;
	struct {
		unsigned int start;
		size_t length;
	} *ranges;
};

struct parse_events_term {
	char *config;
	struct parse_events_array array;
	union {
		char *str;
		u64  num;
	} val;
	int type_val;
	int type_term;
	struct list_head list;
	bool used;
	bool no_value;

	/* error string indexes for within parsed string */
	int err_term;
	int err_val;

	/* Coming from implicit alias */
	bool weak;
};

struct parse_events_error {
	int   num_errors;       /* number of errors encountered */
	int   idx;	/* index in the parsed string */
	char *str;      /* string to display at the index */
	char *help;	/* optional help string */
	int   first_idx;/* as above, but for the first encountered error */
	char *first_str;
	char *first_help;
};

struct parse_events_state;

struct parse_events_ops {
	struct perf_evsel* (*perf_evsel__new)(struct perf_event_attr *attr, int idx, bool init_attr);
	struct perf_evsel* (*perf_evsel__new_tp)(const char *sys, const char *name, int idx);
	void (*perf_evsel__delete)(struct perf_evsel *evsel);

	int (*add_pmu)(struct parse_events_state *parse_state,
		       struct list_head *list, char *pmu_name,
		       struct list_head *head_config,
		       struct list_head *orig_terms,
		       bool auto_merge_stats,
		       bool use_alias);

	int (*add_pmu_multi)(struct parse_events_state *parse_state,
			     char *str, struct list_head *head,
			     struct list_head **listp);

	int (*add_numeric)(struct parse_events_state *parse_state,
			   struct list_head *list,
			   u32 type, u64 config,
			   struct list_head *head_config);
};

struct parse_events_state {
	struct list_head	   list;
	int			   idx;
	int			   nr_groups;
	struct parse_events_error *error;
	struct perf_evlist	  *evlist;
	struct list_head	  *terms;
	int			   stoken;
	struct perf_pmu		  *fake_pmu;
	char			  *hybrid_pmu_name;
	struct parse_events_ops	  *ops;
	bool			   guest;
};

int parse_events_term__num(struct parse_events_term **term,
			   int type_term, char *config, u64 num,
			   bool novalue,
			   int loc_term, int loc_val);
int parse_events_term__str(struct parse_events_term **term,
			   int type_term, char *config, char *str,
			   int loc_term, int loc_val);
int parse_events_term__sym_hw(struct parse_events_term **term,
			      char *config, unsigned idx);
int parse_events_term__clone(struct parse_events_term **new,
			     struct parse_events_term *term);
int parse_events__is_hardcoded_term(struct parse_events_term *term);
void parse_events_term__delete(struct parse_events_term *term);
void parse_events_terms__delete(struct list_head *terms);
void parse_events_terms__purge(struct list_head *terms);
int parse_events_copy_term_list(struct list_head *old,
				 struct list_head **new);

struct perf_evsel *
perf_evsel__add_event(struct parse_events_state *parse_state,
		      struct list_head *list,
		      struct perf_event_attr *attr,
		      bool init_attr,
		      const char *name, const char *metric_id, struct perf_pmu *pmu,
		      struct list_head *config_terms, bool auto_merge_stats,
		      const char *cpu_list);
int parse_events__modifier_event(struct list_head *list, char *str, bool add, bool guest);
int parse_events__modifier_group(struct list_head *list, char *event_mod, bool guest);
void parse_events__handle_error(struct parse_events_error *err, int idx,
				char *str, char *help);
void parse_events_evlist_error(struct parse_events_state *parse_state,
			       int idx, const char *str);
int parse_events_name(struct list_head *list, const char *name);
#endif /* __LIBPERF_PARSE_EVENTS_H */
