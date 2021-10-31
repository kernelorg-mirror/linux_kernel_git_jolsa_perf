// SPDX-License-Identifier: GPL-2.0-only

#include <internal/parse-events.h>
#include <linux/perf_event.h>
#include <linux/zalloc.h>
#include <string.h>
#include <internal/parse-events.h>
#include <stdlib.h>
#include <errno.h>

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
