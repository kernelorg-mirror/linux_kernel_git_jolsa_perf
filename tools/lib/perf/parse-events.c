// SPDX-License-Identifier: GPL-2.0-only

#include <internal/parse-events.h>
#include <linux/perf_event.h>

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

