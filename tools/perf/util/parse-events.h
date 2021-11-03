/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_PARSE_EVENTS_H
#define __PERF_PARSE_EVENTS_H
/*
 * Parse symbolic events/counts passed in as options:
 */

#include <linux/list.h>
#include <stdbool.h>
#include <linux/types.h>
#include <linux/perf_event.h>
#include <string.h>
#include <internal/parse-events.h>

struct list_head;
struct evsel;
struct evlist;
struct parse_events_error;

struct option;
struct perf_pmu;

struct tracepoint_path {
	char *system;
	char *name;
	struct tracepoint_path *next;
};

struct tracepoint_path *tracepoint_id_to_path(u64 config);
struct tracepoint_path *tracepoint_name_to_path(const char *name);
bool have_tracepoints(struct list_head *evlist);

const char *event_type(int type);

int parse_events_option(const struct option *opt, const char *str, int unset);
int parse_events_option_new_evlist(const struct option *opt, const char *str, int unset);
int __parse_events(struct evlist *evlist, const char *str, struct parse_events_error *error,
		   struct perf_pmu *fake_pmu);

static inline int parse_events(struct evlist *evlist, const char *str,
			       struct parse_events_error *err)
{
	return __parse_events(evlist, str, err, NULL);
}

int parse_events_terms(struct list_head *terms, const char *str);
int parse_filter(const struct option *opt, const char *str, int unset);
int exclude_perf(const struct option *opt, const char *arg, int unset);

#define EVENTS_HELP_MAX (128*1024)

enum perf_pmu_event_symbol_type {
	PMU_EVENT_SYMBOL_ERR,		/* not a PMU EVENT */
	PMU_EVENT_SYMBOL,		/* normal style PMU event */
	PMU_EVENT_SYMBOL_PREFIX,	/* prefix of pre-suf style event */
	PMU_EVENT_SYMBOL_SUFFIX,	/* suffix of pre-suf style event */
};

struct perf_pmu_event_symbol {
	char	*symbol;
	enum perf_pmu_event_symbol_type	type;
};

void parse_events__shrink_config_terms(void);
void parse_events__clear_array(struct parse_events_array *a);
int parse_events_add_tracepoint(struct parse_events_state *parse_state,
				struct list_head *list,
				const char *sys, const char *event,
				struct parse_events_error *error,
				struct list_head *head_config);
int parse_events_load_bpf(struct parse_events_state *parse_state,
			  struct list_head *list,
			  char *bpf_file_name,
			  bool source,
			  struct list_head *head_config);
/* Provide this function for perf test */
struct bpf_object;
int parse_events_load_bpf_obj(struct parse_events_state *parse_state,
			      struct list_head *list,
			      struct bpf_object *obj,
			      struct list_head *head_config);
int parse_events_add_numeric(struct parse_events_state *parse_state,
			     struct list_head *list,
			     u32 type, u64 config,
			     struct list_head *head_config);
int parse_events_add_tool(struct parse_events_state *parse_state,
			  struct list_head *list,
			  int tool_event);
int parse_events_add_cache(struct parse_events_state *parse_state,
			   struct list_head *list,
			   char *type, char *op_result1, char *op_result2,
			   struct parse_events_error *error,
			   struct list_head *head_config);
int parse_events_add_breakpoint(struct parse_events_state *parse_state,
				struct list_head *list,
				u64 addr, char *type, u64 len);

struct evsel *parse_events__add_event(struct parse_events_state *parse_state,
				      struct perf_event_attr *attr,
				      const char *name, const char *metric_id,
				      struct perf_pmu *pmu);

enum perf_pmu_event_symbol_type
perf_pmu__parse_check(const char *name);
void parse_events__set_leader(char *name, struct list_head *list,
			      struct parse_events_state *parse_state);

void print_events(const char *event_glob, bool name_only, bool quiet,
		  bool long_desc, bool details_flag, bool deprecated,
		  const char *pmu_name);

void print_symbol_events(const char *event_glob, unsigned type,
				struct event_symbol *syms, unsigned max,
				bool name_only);
void print_tool_events(const char *event_glob, bool name_only);
void print_tracepoint_events(const char *subsys_glob, const char *event_glob,
			     bool name_only);
int print_hwcache_events(const char *event_glob, bool name_only);
void print_sdt_events(const char *subsys_glob, const char *event_glob,
		      bool name_only);
int is_valid_tracepoint(const char *event_string);

int valid_event_mount(const char *eventfs);
char *parse_events_formats_error_string(char *additional_terms);

void parse_events_print_error(struct parse_events_error *err,
			      const char *event);

#ifdef HAVE_LIBELF_SUPPORT
/*
 * If the probe point starts with '%',
 * or starts with "sdt_" and has a ':' but no '=',
 * then it should be a SDT/cached probe point.
 */
static inline bool is_sdt_event(char *str)
{
	return (str[0] == '%' ||
		(!strncmp(str, "sdt_", 4) &&
		 !!strchr(str, ':') && !strchr(str, '=')));
}
#else
static inline bool is_sdt_event(char *str __maybe_unused)
{
	return false;
}
#endif /* HAVE_LIBELF_SUPPORT */

int perf_pmu__test_parse_init(void);

struct evsel *parse_events__add_event_hybrid(struct parse_events_state *parse_state,
					     struct list_head *list,
					     struct perf_event_attr *attr,
					     const char *name,
					     const char *metric_id,
					     struct perf_pmu *pmu,
					     struct list_head *config_terms);

#endif /* __PERF_PARSE_EVENTS_H */
