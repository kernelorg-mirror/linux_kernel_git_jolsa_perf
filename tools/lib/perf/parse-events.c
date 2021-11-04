// SPDX-License-Identifier: GPL-2.0-only

#include <internal/parse-events.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <linux/zalloc.h>
#include <string.h>
#include <internal/parse-events.h>
#include <internal/pmu.h>
#include <internal/evlist.h>
#include <internal/evsel.h>
#include <perf/cpumap.h>
#include <perf/evlist.h>
#include <perf/evsel.h>
#include <stdlib.h>
#include <errno.h>
#include <asm/bug.h>
#include "internal.h"
#include "parse-events-bison.h"
#define YY_EXTRA_TYPE void*
#include "parse-events-flex.h"

#ifdef PARSER_DEBUG
extern int parse_events_debug;
#endif

struct event_symbol event_symbols_hw[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES] = {
		.symbol = "cpu-cycles",
		.alias  = "cycles",
	},
	[PERF_COUNT_HW_INSTRUCTIONS] = {
		.symbol = "instructions",
		.alias  = "",
	},
	[PERF_COUNT_HW_CACHE_REFERENCES] = {
		.symbol = "cache-references",
		.alias  = "",
	},
	[PERF_COUNT_HW_CACHE_MISSES] = {
		.symbol = "cache-misses",
		.alias  = "",
	},
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = {
		.symbol = "branch-instructions",
		.alias  = "branches",
	},
	[PERF_COUNT_HW_BRANCH_MISSES] = {
		.symbol = "branch-misses",
		.alias  = "",
	},
	[PERF_COUNT_HW_BUS_CYCLES] = {
		.symbol = "bus-cycles",
		.alias  = "",
	},
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND] = {
		.symbol = "stalled-cycles-frontend",
		.alias  = "idle-cycles-frontend",
	},
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND] = {
		.symbol = "stalled-cycles-backend",
		.alias  = "idle-cycles-backend",
	},
	[PERF_COUNT_HW_REF_CPU_CYCLES] = {
		.symbol = "ref-cycles",
		.alias  = "",
	},
};

struct event_symbol event_symbols_sw[PERF_COUNT_SW_MAX] = {
	[PERF_COUNT_SW_CPU_CLOCK] = {
		.symbol = "cpu-clock",
		.alias  = "",
	},
	[PERF_COUNT_SW_TASK_CLOCK] = {
		.symbol = "task-clock",
		.alias  = "",
	},
	[PERF_COUNT_SW_PAGE_FAULTS] = {
		.symbol = "page-faults",
		.alias  = "faults",
	},
	[PERF_COUNT_SW_CONTEXT_SWITCHES] = {
		.symbol = "context-switches",
		.alias  = "cs",
	},
	[PERF_COUNT_SW_CPU_MIGRATIONS] = {
		.symbol = "cpu-migrations",
		.alias  = "migrations",
	},
	[PERF_COUNT_SW_PAGE_FAULTS_MIN] = {
		.symbol = "minor-faults",
		.alias  = "",
	},
	[PERF_COUNT_SW_PAGE_FAULTS_MAJ] = {
		.symbol = "major-faults",
		.alias  = "",
	},
	[PERF_COUNT_SW_ALIGNMENT_FAULTS] = {
		.symbol = "alignment-faults",
		.alias  = "",
	},
	[PERF_COUNT_SW_EMULATION_FAULTS] = {
		.symbol = "emulation-faults",
		.alias  = "",
	},
	[PERF_COUNT_SW_DUMMY] = {
		.symbol = "dummy",
		.alias  = "",
	},
	[PERF_COUNT_SW_BPF_OUTPUT] = {
		.symbol = "bpf-output",
		.alias  = "",
	},
	[PERF_COUNT_SW_CGROUP_SWITCHES] = {
		.symbol = "cgroup-switches",
		.alias  = "",
	},
};

/*
 * Update according to parse-events.l
 */
const char *config_term_names[__PARSE_EVENTS__TERM_TYPE_NR] = {
	[PARSE_EVENTS__TERM_TYPE_USER]			= "<sysfs term>",
	[PARSE_EVENTS__TERM_TYPE_CONFIG]		= "config",
	[PARSE_EVENTS__TERM_TYPE_CONFIG1]		= "config1",
	[PARSE_EVENTS__TERM_TYPE_CONFIG2]		= "config2",
	[PARSE_EVENTS__TERM_TYPE_NAME]			= "name",
	[PARSE_EVENTS__TERM_TYPE_SAMPLE_PERIOD]		= "period",
	[PARSE_EVENTS__TERM_TYPE_SAMPLE_FREQ]		= "freq",
	[PARSE_EVENTS__TERM_TYPE_BRANCH_SAMPLE_TYPE]	= "branch_type",
	[PARSE_EVENTS__TERM_TYPE_TIME]			= "time",
	[PARSE_EVENTS__TERM_TYPE_CALLGRAPH]		= "call-graph",
	[PARSE_EVENTS__TERM_TYPE_STACKSIZE]		= "stack-size",
	[PARSE_EVENTS__TERM_TYPE_NOINHERIT]		= "no-inherit",
	[PARSE_EVENTS__TERM_TYPE_INHERIT]		= "inherit",
	[PARSE_EVENTS__TERM_TYPE_MAX_STACK]		= "max-stack",
	[PARSE_EVENTS__TERM_TYPE_MAX_EVENTS]		= "nr",
	[PARSE_EVENTS__TERM_TYPE_OVERWRITE]		= "overwrite",
	[PARSE_EVENTS__TERM_TYPE_NOOVERWRITE]		= "no-overwrite",
	[PARSE_EVENTS__TERM_TYPE_DRV_CFG]		= "driver-config",
	[PARSE_EVENTS__TERM_TYPE_PERCORE]		= "percore",
	[PARSE_EVENTS__TERM_TYPE_AUX_OUTPUT]		= "aux-output",
	[PARSE_EVENTS__TERM_TYPE_AUX_SAMPLE_SIZE]	= "aux-sample-size",
	[PARSE_EVENTS__TERM_TYPE_METRIC_ID]		= "metric-id",
};

static int new_term(struct parse_events_term **_term,
		    struct parse_events_term *temp,
		    char *str, u64 num)
{
	struct parse_events_term *term;

	term = malloc(sizeof(*term));
	if (!term)
		return -ENOMEM;

	*term = *temp;
	INIT_LIST_HEAD(&term->list);
	term->weak = false;

	switch (term->type_val) {
	case PARSE_EVENTS__TERM_TYPE_NUM:
		term->val.num = num;
		break;
	case PARSE_EVENTS__TERM_TYPE_STR:
		term->val.str = str;
		break;
	default:
		free(term);
		return -EINVAL;
	}

	*_term = term;
	return 0;
}

int parse_events_term__num(struct parse_events_term **term,
			   int type_term, char *config, u64 num,
			   bool no_value,
			   int loc_term, int loc_val)
{
	struct parse_events_term temp = {
		.type_val  = PARSE_EVENTS__TERM_TYPE_NUM,
		.type_term = type_term,
		.config    = config ? : strdup(config_term_names[type_term]),
		.no_value  = no_value,
		.err_term  = loc_term,
		.err_val   = loc_val,
	};

	return new_term(term, &temp, NULL, num);
}

int parse_events_term__str(struct parse_events_term **term,
			   int type_term, char *config, char *str,
			   int loc_term, int loc_val)
{
	struct parse_events_term temp = {
		.type_val  = PARSE_EVENTS__TERM_TYPE_STR,
		.type_term = type_term,
		.config    = config,
		.err_term  = loc_term,
		.err_val   = loc_val,
	};

	return new_term(term, &temp, str, 0);
}

int parse_events_term__sym_hw(struct parse_events_term **term,
			      char *config, unsigned idx)
{
	struct event_symbol *sym;
	char *str;
	struct parse_events_term temp = {
		.type_val  = PARSE_EVENTS__TERM_TYPE_STR,
		.type_term = PARSE_EVENTS__TERM_TYPE_USER,
		.config    = config,
	};

	if (!temp.config) {
		temp.config = strdup("event");
		if (!temp.config)
			return -ENOMEM;
	}
	BUG_ON(idx >= PERF_COUNT_HW_MAX);
	sym = &event_symbols_hw[idx];

	str = strdup(sym->symbol);
	if (!str)
		return -ENOMEM;
	return new_term(term, &temp, str, 0);
}

int parse_events_term__clone(struct parse_events_term **new,
			     struct parse_events_term *term)
{
	char *str;
	struct parse_events_term temp = {
		.type_val  = term->type_val,
		.type_term = term->type_term,
		.config    = NULL,
		.err_term  = term->err_term,
		.err_val   = term->err_val,
	};

	if (term->config) {
		temp.config = strdup(term->config);
		if (!temp.config)
			return -ENOMEM;
	}
	if (term->type_val == PARSE_EVENTS__TERM_TYPE_NUM)
		return new_term(new, &temp, NULL, term->val.num);

	str = strdup(term->val.str);
	if (!str)
		return -ENOMEM;
	return new_term(new, &temp, str, 0);
}

int parse_events__is_hardcoded_term(struct parse_events_term *term)
{
	return term->type_term != PARSE_EVENTS__TERM_TYPE_USER;
}

void parse_events_term__delete(struct parse_events_term *term)
{
	if (term->array.nr_ranges)
		zfree(&term->array.ranges);

	if (term->type_val != PARSE_EVENTS__TERM_TYPE_NUM)
		zfree(&term->val.str);

	zfree(&term->config);
	free(term);
}

int parse_events_copy_term_list(struct list_head *old,
				 struct list_head **new)
{
	struct parse_events_term *term, *n;
	int ret;

	if (!old) {
		*new = NULL;
		return 0;
	}

	*new = malloc(sizeof(struct list_head));
	if (!*new)
		return -ENOMEM;
	INIT_LIST_HEAD(*new);

	list_for_each_entry (term, old, list) {
		ret = parse_events_term__clone(&n, term);
		if (ret)
			return ret;
		list_add_tail(&n->list, *new);
	}
	return 0;
}

void parse_events_terms__purge(struct list_head *terms)
{
	struct parse_events_term *term, *h;

	list_for_each_entry_safe(term, h, terms, list) {
		list_del_init(&term->list);
		parse_events_term__delete(term);
	}
}

void parse_events_terms__delete(struct list_head *terms)
{
	if (!terms)
		return;
	parse_events_terms__purge(terms);
	free(terms);
}

struct perf_evsel *
perf_evsel__add_event(struct parse_events_state *parse_state,
		      struct list_head *list,
		      struct perf_event_attr *attr,
		      bool init_attr,
		      const char *name, const char *metric_id, struct perf_pmu *pmu,
		      struct list_head *config_terms, bool auto_merge_stats,
		      const char *cpu_list)
{
	struct perf_evsel *evsel;
	struct perf_cpu_map *cpus = pmu ? perf_cpu_map__get(pmu->cpus) :
			       cpu_list ? perf_cpu_map__new(cpu_list) : NULL;
	int *idx = &parse_state->idx;

	if (pmu && attr->type == PERF_TYPE_RAW)
		perf_pmu__warn_invalid_config(pmu, attr->config, name);

	evsel = parse_state->ops->perf_evsel__new(attr, *idx, init_attr);
	if (!evsel) {
		perf_cpu_map__put(cpus);
		return NULL;
	}

	(*idx)++;
	evsel->cpus = cpus;
	evsel->own_cpus = perf_cpu_map__get(cpus);
	evsel->system_wide = pmu ? pmu->is_uncore : false;
	evsel->auto_merge_stats = auto_merge_stats;

	if (name)
		evsel->name = strdup(name);

	if (metric_id)
		evsel->metric_id = strdup(metric_id);

	if (config_terms)
		list_splice_init(config_terms, &evsel->config_terms);

	if (list)
		list_add_tail(&evsel->node, list);

	return evsel;
}

struct event_modifier {
	int eu;
	int ek;
	int eh;
	int eH;
	int eG;
	int eI;
	int precise;
	int precise_max;
	int exclude_GH;
	int sample_read;
	int pinned;
	int weak;
	int exclusive;
	int bpf_counter;
};

static int get_event_modifier(struct event_modifier *mod, char *str,
			       struct perf_evsel *evsel, bool guest)
{
	int eu = evsel ? evsel->attr.exclude_user : 0;
	int ek = evsel ? evsel->attr.exclude_kernel : 0;
	int eh = evsel ? evsel->attr.exclude_hv : 0;
	int eH = evsel ? evsel->attr.exclude_host : 0;
	int eG = evsel ? evsel->attr.exclude_guest : 0;
	int eI = evsel ? evsel->attr.exclude_idle : 0;
	int precise = evsel ? evsel->attr.precise_ip : 0;
	int precise_max = 0;
	int sample_read = 0;
	int pinned = evsel ? evsel->attr.pinned : 0;
	int exclusive = evsel ? evsel->attr.exclusive : 0;

	int exclude = eu | ek | eh;
	int exclude_GH = evsel ? evsel->exclude_GH : 0;
	int weak = 0;
	int bpf_counter = 0;

	memset(mod, 0, sizeof(*mod));

	while (*str) {
		if (*str == 'u') {
			if (!exclude)
				exclude = eu = ek = eh = 1;
			if (!exclude_GH && !guest)
				eG = 1;
			eu = 0;
		} else if (*str == 'k') {
			if (!exclude)
				exclude = eu = ek = eh = 1;
			ek = 0;
		} else if (*str == 'h') {
			if (!exclude)
				exclude = eu = ek = eh = 1;
			eh = 0;
		} else if (*str == 'G') {
			if (!exclude_GH)
				exclude_GH = eG = eH = 1;
			eG = 0;
		} else if (*str == 'H') {
			if (!exclude_GH)
				exclude_GH = eG = eH = 1;
			eH = 0;
		} else if (*str == 'I') {
			eI = 1;
		} else if (*str == 'p') {
			precise++;
			/* use of precise requires exclude_guest */
			if (!exclude_GH)
				eG = 1;
		} else if (*str == 'P') {
			precise_max = 1;
		} else if (*str == 'S') {
			sample_read = 1;
		} else if (*str == 'D') {
			pinned = 1;
		} else if (*str == 'e') {
			exclusive = 1;
		} else if (*str == 'W') {
			weak = 1;
		} else if (*str == 'b') {
			bpf_counter = 1;
		} else
			break;

		++str;
	}

	/*
	 * precise ip:
	 *
	 *  0 - SAMPLE_IP can have arbitrary skid
	 *  1 - SAMPLE_IP must have constant skid
	 *  2 - SAMPLE_IP requested to have 0 skid
	 *  3 - SAMPLE_IP must have 0 skid
	 *
	 *  See also PERF_RECORD_MISC_EXACT_IP
	 */
	if (precise > 3)
		return -EINVAL;

	mod->eu = eu;
	mod->ek = ek;
	mod->eh = eh;
	mod->eH = eH;
	mod->eG = eG;
	mod->eI = eI;
	mod->precise = precise;
	mod->precise_max = precise_max;
	mod->exclude_GH = exclude_GH;
	mod->sample_read = sample_read;
	mod->pinned = pinned;
	mod->weak = weak;
	mod->bpf_counter = bpf_counter;
	mod->exclusive = exclusive;

	return 0;
}

/*
 * Basic modifier sanity check to validate it contains only one
 * instance of any modifier (apart from 'p') present.
 */
static int check_modifier(char *str)
{
	char *p = str;

	/* The sizeof includes 0 byte as well. */
	if (strlen(str) > (sizeof("ukhGHpppPSDIWeb") - 1))
		return -1;

	while (*p) {
		if (*p != 'p' && strchr(p + 1, *p))
			return -1;
		p++;
	}

	return 0;
}

int parse_events__modifier_event(struct list_head *list, char *str,
				 bool add, bool guest)
{
	struct perf_evsel *evsel;
	struct event_modifier mod;

	if (str == NULL)
		return 0;

	if (check_modifier(str))
		return -EINVAL;

	if (!add && get_event_modifier(&mod, str, NULL, guest))
		return -EINVAL;

	__perf_evlist__for_each_entry(list, evsel) {
		if (add && get_event_modifier(&mod, str, evsel, guest))
			return -EINVAL;

		evsel->attr.exclude_user   = mod.eu;
		evsel->attr.exclude_kernel = mod.ek;
		evsel->attr.exclude_hv     = mod.eh;
		evsel->attr.precise_ip     = mod.precise;
		evsel->attr.exclude_host   = mod.eH;
		evsel->attr.exclude_guest  = mod.eG;
		evsel->attr.exclude_idle   = mod.eI;
		evsel->exclude_GH          = mod.exclude_GH;
		evsel->sample_read         = mod.sample_read;
		evsel->precise_max         = mod.precise_max;
		evsel->weak_group          = mod.weak;
		evsel->bpf_counter         = mod.bpf_counter;

		if (perf_evsel__is_group_leader(evsel)) {
			evsel->attr.pinned = mod.pinned;
			evsel->attr.exclusive = mod.exclusive;
		}
	}

	return 0;
}

int parse_events__modifier_group(struct list_head *list,
				 char *event_mod, bool guest)
{
	return parse_events__modifier_event(list, event_mod, true, guest);
}

void parse_events__handle_error(struct parse_events_error *err, int idx,
				char *str, char *help)
{
	if (WARN(!str, "WARNING: failed to provide error string\n")) {
		free(help);
		return;
	}
	switch (err->num_errors) {
	case 0:
		err->idx = idx;
		err->str = str;
		err->help = help;
		break;
	case 1:
		err->first_idx = err->idx;
		err->idx = idx;
		err->first_str = err->str;
		err->str = str;
		err->first_help = err->help;
		err->help = help;
		break;
	default:
		pr_debug("Multiple errors dropping message: %s (%s)\n",
			err->str, err->help);
		free(err->str);
		err->str = str;
		free(err->help);
		err->help = help;
		break;
	}
	err->num_errors++;
}

void parse_events_evlist_error(struct parse_events_state *parse_state,
			       int idx, const char *str)
{
	if (!parse_state->error)
		return;

	parse_events__handle_error(parse_state->error, idx, strdup(str), NULL);
}

int parse_events_name(struct list_head *list, const char *name)
{
	struct perf_evsel *evsel;

	__perf_evlist__for_each_entry(list, evsel) {
		if (!evsel->name)
			evsel->name = strdup(name);
	}

	return 0;
}

int parse_events__scanner(const char *str,
			  struct parse_events_state *parse_state,
			  bool terms)
{
	YY_BUFFER_STATE buffer;
	void *scanner;
	int ret;

	if (terms)
		parse_state->stoken = PE_START_TERMS;
	else
		parse_state->stoken = PE_START_EVENTS;

	ret = parse_events_lex_init_extra(parse_state, &scanner);
	if (ret)
		return ret;

	buffer = parse_events__scan_string(str, scanner);

#ifdef PARSER_DEBUG
	parse_events_debug = 1;
	parse_events_set_debug(1, scanner);
#endif
	ret = parse_events_parse(parse_state, scanner);

	parse_events__flush_buffer(buffer, scanner);
	parse_events__delete_buffer(buffer, scanner);
	parse_events_lex_destroy(scanner);
	return ret;
}

static int
parse_breakpoint_type(const char *type, struct perf_event_attr *attr)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (!type || !type[i])
			break;

#define CHECK_SET_TYPE(bit)		\
do {					\
	if (attr->bp_type & bit)	\
		return -EINVAL;		\
	else				\
		attr->bp_type |= bit;	\
} while (0)

		switch (type[i]) {
		case 'r':
			CHECK_SET_TYPE(HW_BREAKPOINT_R);
			break;
		case 'w':
			CHECK_SET_TYPE(HW_BREAKPOINT_W);
			break;
		case 'x':
			CHECK_SET_TYPE(HW_BREAKPOINT_X);
			break;
		default:
			return -EINVAL;
		}
	}

#undef CHECK_SET_TYPE

	if (!attr->bp_type) /* Default */
		attr->bp_type = HW_BREAKPOINT_R | HW_BREAKPOINT_W;

	return 0;
}

int parse_events_add_breakpoint(struct parse_events_state *parse_state,
				struct list_head *list,
				u64 addr, char *type, u64 len)
{
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.bp_addr = addr;

	if (parse_breakpoint_type(type, &attr))
		return -EINVAL;

	/* Provide some defaults if len is not specified */
	if (!len) {
		if (attr.bp_type == HW_BREAKPOINT_X)
			len = sizeof(long);
		else
			len = HW_BREAKPOINT_LEN_4;
	}

	attr.bp_len = len;

	attr.type = PERF_TYPE_BREAKPOINT;
	attr.sample_period = 1;

	return perf_evsel__add_event(parse_state, list, &attr,
				     /*init_attr*/true, /*name=*/NULL, /*mertic_id=*/NULL,
				     /*pmu=*/NULL, /*config_terms=*/NULL,
				     /*auto_merge_stats=*/false, /*cpu_list=*/NULL) ? 0 : -ENOENT;
}

static struct perf_evsel*
perf_evsel__new_idx(struct perf_event_attr *attr, int idx, bool init_attr __maybe_unused)
{
	struct perf_evsel *evsel = perf_evsel__new(attr);

	if (evsel)
		perf_evsel__init(evsel, attr, idx);
	return evsel;
}

static struct perf_evsel*
perf_evsel__newtp_idx(const char *sys __maybe_unused, const char *name __maybe_unused,
		      int idx __maybe_unused)
{
	return NULL;
}

static void perf_evsel__delete_helper(struct perf_evsel *evsel)
{
	perf_evsel__delete(evsel);
}

static
int parse_events_add_pmu(struct parse_events_state *parse_state __maybe_unused,
			 struct list_head *list __maybe_unused,
			 char *pmu_name __maybe_unused,
			 struct list_head *head_config __maybe_unused,
			 struct list_head *orig_terms __maybe_unused,
			 bool auto_merge_stats __maybe_unused,
			 bool use_alias __maybe_unused)
{
	return -ENOTSUP;
}

static int
parse_events_multi_pmu_add(struct parse_events_state *parse_state __maybe_unused,
			   char *str __maybe_unused,
			   struct list_head *head __maybe_unused,
			   struct list_head **listp __maybe_unused)
{
	return -ENOTSUP;
}

static int
parse_events_add_numeric(struct parse_events_state *parse_state __maybe_unused,
			 struct list_head *list __maybe_unused,
			 u32 type __maybe_unused, u64 config __maybe_unused,
			 struct list_head *head_config __maybe_unused)
{
	return -ENOTSUP;
}

static int
parse_events_add_cache(struct parse_events_state *parse_state __maybe_unused,
		       struct list_head *list __maybe_unused,
		       char *type __maybe_unused, char *op_result1 __maybe_unused,
		       char *op_result2 __maybe_unused,
		       struct parse_events_error *err __maybe_unused,
		       struct list_head *head_config __maybe_unused)
{
	return -ENOTSUP;
}

static int
parse_events_add_tracepoint(struct parse_events_state *parse_state __maybe_unused,
			    struct list_head *list __maybe_unused,
			    const char *sys __maybe_unused, const char *event __maybe_unused,
			    struct parse_events_error *err __maybe_unused,
			    struct list_head *head_config __maybe_unused)
{
	return -ENOTSUP;
}

static int
parse_events_load_bpf(struct parse_events_state *parse_state __maybe_unused,
		      struct list_head *list __maybe_unused,
		      char *bpf_file_name __maybe_unused,
		      bool source __maybe_unused,
		      struct list_head *head_config __maybe_unused)
{
	return -ENOTSUP;
}

static void
parse_events_set_leader(char *name, struct list_head *list,
			struct parse_events_state *parse_state)
{
	struct perf_evsel *leader;

	if (list_empty(list))
                return;

	__perf_evlist__set_leader(list);
	leader = list_entry(list->next, struct perf_evsel, node);
	leader->group_name = name ? strdup(name) : NULL;
}

static enum perf_pmu_event_symbol_type
perf_pmu__parse_check(const char *name __maybe_unused)
{
	return PMU_EVENT_SYMBOL_ERR;
}

static struct parse_events_ops parse_state_ops = {
	.perf_evsel__new    = perf_evsel__new_idx,
	.perf_evsel__new_tp = perf_evsel__newtp_idx,
	.perf_evsel__delete = perf_evsel__delete_helper,
	.add_pmu            = parse_events_add_pmu,
	.add_pmu_multi      = parse_events_multi_pmu_add,
	.add_numeric        = parse_events_add_numeric,
	.add_cache          = parse_events_add_cache,
	.add_breakpoint     = parse_events_add_breakpoint,
	.add_tracepoint     = parse_events_add_tracepoint,
	.add_bpf            = parse_events_load_bpf,
	.set_leader         = parse_events_set_leader,
	.parse_check        = perf_pmu__parse_check,
};

static bool perf_evsel__has_leader(struct perf_evsel *evsel, struct perf_evsel *leader)
{
	return evsel->leader == leader;
}

static void perf_evlist__splice_list_tail(struct perf_evlist *evlist, struct list_head *list)
{
	while (!list_empty(list)) {
		struct perf_evsel *evsel, *temp, *leader = NULL;

		__perf_evlist__for_each_entry_safe(list, temp, evsel) {
			list_del_init(&evsel->node);
			perf_evlist__add(evlist, evsel);
			leader = evsel;
			break;
		}

		__perf_evlist__for_each_entry_safe(list, temp, evsel) {
			if (perf_evsel__has_leader(evsel, leader)) {
				list_del_init(&evsel->node);
				perf_evlist__add(evlist, evsel);
			}
		}
	}
}

int libperf_parse_events(struct perf_evlist *evlist, const char *str)
{
	struct parse_events_state parse_state = {
		.list     = LIST_HEAD_INIT(parse_state.list),
		.idx      = evlist->nr_entries,
		.evlist   = evlist,
		.ops      = &parse_state_ops,
	};
	int err;

	err = parse_events__scanner(str, &parse_state, false);

	if (!err && list_empty(&parse_state.list)) {
		WARN_ONCE(true, "WARNING: event parser found nothing\n");
		return -1;
	}

	perf_evlist__splice_list_tail(evlist, &parse_state.list);

	if (!err)
		evlist->nr_groups += parse_state.nr_groups;

	return err;
}
