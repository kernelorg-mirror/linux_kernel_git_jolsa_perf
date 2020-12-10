/* SPDX-License-Identifier: GPL-2.0 */
/*
 * perf stat --iiostat
 *
 * Copyright (C) 2020, Intel Corporation
 *
 * Authors: Alexander Antonov <alexander.antonov@linux.intel.com>
 */

#ifndef _IIOSTAT_H
#define _IIOSTAT_H

#include <subcmd/parse-options.h>
#include "util/stat.h"
#include "util/parse-events.h"
#include "util/evlist.h"

struct option;
struct perf_stat_config;
struct evlist;
struct timespec;

int iiostat_parse(const struct option *opt, const char *str,
		  int unset __maybe_unused);
void iiostat_prefix(struct perf_stat_config *config, struct evlist *evlist,
		    char *prefix, struct timespec *ts);
void iiostat_print_metric(struct perf_stat_config *config, struct evsel *evsel,
			  struct perf_stat_output_ctx *out);
int iiostat_show_root_ports(struct evlist *evlist,
			    struct perf_stat_config *config);
void iiostat_delete_root_ports(struct evlist *evlist);

#endif /* _IIOSTAT_H */
