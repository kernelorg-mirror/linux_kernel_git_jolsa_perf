// SPDX-License-Identifier: GPL-2.0
/*
 * perf stat --iiostat
 *
 * Copyright (C) 2020, Intel Corporation
 *
 * Authors: Alexander Antonov <alexander.antonov@linux.intel.com>
 */

#include <api/fs/fs.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <regex.h>
#include "util/cpumap.h"
#include "util/debug.h"
#include "util/iiostat.h"
#include "util/counts.h"
#include "path.h"

#ifndef MAX_PATH
#define MAX_PATH 1024
#endif

#define UNCORE_IIO_PMU_PATH	"devices/uncore_iio_%d"
#define SYSFS_UNCORE_PMU_PATH	"%s/"UNCORE_IIO_PMU_PATH
#define PLATFORM_MAPPING_PATH	UNCORE_IIO_PMU_PATH"/die%d"

enum iiostat_mode_t {
	IIOSTAT_NONE		= -1,
	IIOSTAT_RUN		= 0,
	IIOSTAT_SHOW		= 1
};

static enum iiostat_mode_t iiostat_mode = IIOSTAT_NONE;

/*
 * Each metric requiries only one IIO event which increments at every 4B transfer
 * in corresponding direction. The formulas to compute metrics are generic:
 *     #EventCount * 4B / (1024 * 1024)
 */
static const char * const iiostat_metrics[] = {
	"Inbound Read(MB)",
	"Inbound Write(MB)",
	"Outbound Read(MB)",
	"Outbound Write(MB)",
};

static inline int iiostat_metrics_count(void)
{
	return sizeof(iiostat_metrics) / sizeof(char *);
}

static const char *iiostat_metric_by_idx(int idx)
{
	return *(iiostat_metrics + idx % iiostat_metrics_count());
}

struct iio_root_port {
	u32 domain;
	u8 bus;
	u8 die;
	u8 pmu_idx;
	int idx;
};

struct iio_root_ports_list {
	struct iio_root_port **rps;
	int nr_entries;
};

static void iio_root_port_show(FILE *output, const struct iio_root_port * const rp)
{
	if (output && rp)
		fprintf(output, "S%d-uncore_iio_%d<%04x:%02x>\n",
			rp->die, rp->pmu_idx, rp->domain, rp->bus);
}

static struct iio_root_port *iio_root_port_new(u32 domain, u8 bus, u8 die, u8 pmu_idx)
{
	struct iio_root_port *p = calloc(1, sizeof(*p));

	if (p) {
		p->domain = domain;
		p->bus = bus;
		p->die = die;
		p->pmu_idx = pmu_idx;
	}
	return p;
}

static struct iio_root_ports_list *iio_root_ports_list_new(void)
{
	struct iio_root_ports_list *list = calloc(1, sizeof(*list));

	if (list) {
		list->rps = calloc(1, sizeof(struct iio_root_port *));
		if (!list->rps) {
			free(list);
			list = NULL;
		}
	}

	return list;
}

static void iio_root_ports_list_free(struct iio_root_ports_list *list)
{
	int idx;

	if (list) {
		for (idx = 0; idx < list->nr_entries; idx++)
			free(list->rps[idx]);
		free(list->rps);
		free(list);
	}
}

static
struct iio_root_port *iio_root_port_find_by_notation(const struct iio_root_ports_list * const list,
						     u32 domain, u8 bus)
{
	int idx;
	struct iio_root_port *rp;

	if (list) {
		for (idx = 0; idx < list->nr_entries; idx++) {
			rp = list->rps[idx];
			if (rp && rp->domain == domain && rp->bus == bus)
				return rp;
		}
	}
	return NULL;
}

static int iio_root_ports_list_insert(struct iio_root_ports_list *list,
				      struct iio_root_port * const rp)
{
	struct iio_root_port **tmp_buf;

	if (list && rp) {
		rp->idx = list->nr_entries++;
		/* One more for NULL.*/
		tmp_buf = realloc(list->rps, (list->nr_entries + 1) * sizeof(*list->rps));
		if (!tmp_buf) {
			pr_err("Failed to realloc memory\n");
			return -ENOMEM;
		}
		tmp_buf[rp->idx] = rp;
		tmp_buf[list->nr_entries] = NULL;
		list->rps = tmp_buf;
	}
	return 0;
}

static int uncore_pmu_iio_platform_mapping(u8 pmu_idx, struct iio_root_ports_list * const list)
{
	char *buf;
	char path[MAX_PATH];
	u32 domain;
	u8 bus;
	struct iio_root_port *rp;
	size_t size;
	int ret;

	for (int die = 0; die < cpu__max_node(); die++) {
		scnprintf(path, MAX_PATH, PLATFORM_MAPPING_PATH, pmu_idx, die);
		if (sysfs__read_str(path, &buf, &size) < 0) {
			if (pmu_idx)
				goto out;
			pr_err("Mode iiostat is not supported\n");
			return -1;
		}
		ret = sscanf(buf, "%04x:%02hhx", &domain, &bus);
		free(buf);
		if (ret != 2) {
			pr_err("Invalid mapping data: iio_%d; die%d\n", pmu_idx, die);
			return -1;
		}
		rp = iio_root_port_new(domain, bus, die, pmu_idx);
		if (!rp || iio_root_ports_list_insert(list, rp)) {
			free(rp);
			return -ENOMEM;
		}
	}
out:
	return 0;
}

static u8 iio_pmu_count(void)
{
	u8 pmu_idx = 0;
	char path[MAX_PATH];
	const char *sysfs = sysfs__mountpoint();

	if (sysfs) {
		for (;; pmu_idx++) {
			snprintf(path, sizeof(path), SYSFS_UNCORE_PMU_PATH,
				 sysfs, pmu_idx);
			if (access(path, F_OK) != 0)
				break;
		}
	}
	return pmu_idx;
}

static int iio_root_ports_scan(struct iio_root_ports_list **list)
{
	int ret = -ENOMEM;
	struct iio_root_ports_list *tmp_list;
	u8 pmu_count = iio_pmu_count();

	if (!pmu_count) {
		pr_err("Unsupported uncore pmu configuration\n");
		return -1;
	}

	tmp_list = iio_root_ports_list_new();
	if (!tmp_list)
		goto err;

	for (u8 pmu_idx = 0; pmu_idx < pmu_count; pmu_idx++) {
		ret = uncore_pmu_iio_platform_mapping(pmu_idx, tmp_list);
		if (ret)
			break;
	}
err:
	if (!ret)
		*list = tmp_list;
	else
		iio_root_ports_list_free(tmp_list);

	return ret;
}

static int iio_root_port_parse_str(u32 *domain, u8 *bus, char *str)
{
	int ret;
	regex_t regex;
	/*
	 * Expected format domain:bus:
	 * Valid domain range [0:ffff]
	 * Valid bus range [0:ff]
	 * Example: 0000:af, 0:3d, 01:7
	 */
	regcomp(&regex, "^([a-f0-9A-F]{1,}):([a-f0-9A-F]{1,2})$", REG_EXTENDED);
	ret = regexec(&regex, str, 0, NULL, 0);
	if (ret || sscanf(str, "%08x:%02hhx", domain, bus) != 2)
		pr_warning("Unrecognized root port format: %s\n"
			   "Please use the following format:\n"
			   "\t [domain]:[bus]\n"
			   "\t for example: 0000:3d\n", str);

	regfree(&regex);
	return ret;
}

static int iio_root_ports_list_filter(struct iio_root_ports_list **list,
				      const char *filter)
{
	char *tok, *tmp, *filter_copy = NULL;
	struct iio_root_port *rp;
	u32 domain;
	u8 bus;
	int ret = -ENOMEM;
	struct iio_root_ports_list *tmp_list = iio_root_ports_list_new();

	if (!tmp_list)
		goto err;

	filter_copy = strdup(filter);
	if (!filter_copy)
		goto err;

	for (tok = strtok_r(filter_copy, ",", &tmp); tok; tok = strtok_r(NULL, ",", &tmp)) {
		if (!iio_root_port_parse_str(&domain, &bus, tok)) {
			rp = iio_root_port_find_by_notation(*list, domain, bus);
			if (rp) {
				(*list)->rps[rp->idx] = NULL;
				ret = iio_root_ports_list_insert(tmp_list, rp);
				if (ret) {
					free(rp);
					goto err;
				}
			} else if (!iio_root_port_find_by_notation(tmp_list, domain, bus))
				pr_warning("Root port %04x:%02x were not found\n", domain, bus);
		}
	}

	if (tmp_list->nr_entries == 0) {
		pr_err("Requested root ports were not found\n");
		ret = -EINVAL;
	}
err:
	iio_root_ports_list_free(*list);
	if (ret)
		iio_root_ports_list_free(tmp_list);
	else
		*list = tmp_list;

	free(filter_copy);
	return ret;
}

static int iiostat_event_group(struct evlist *evl, struct iio_root_ports_list *list)
{
	int ret;
	struct iio_root_port **rp;
	const char *iiostat_cmd_template =
		"{uncore_iio_%x/event=0x83,umask=0x04,ch_mask=0xF,fc_mask=0x07/,\
		uncore_iio_%x/event=0x83,umask=0x01,ch_mask=0xF,fc_mask=0x07/,\
		uncore_iio_%x/event=0xc0,umask=0x04,ch_mask=0xF,fc_mask=0x07/,\
		uncore_iio_%x/event=0xc0,umask=0x01,ch_mask=0xF,fc_mask=0x07/}";
	const int len_template = strlen(iiostat_cmd_template) + 1;
	struct evsel *evsel = NULL;
	int metrics_count = iiostat_metrics_count();
	char *iiostat_cmd = calloc(len_template, 1);

	if (!iiostat_cmd)
		return -ENOMEM;

	for (rp = list->rps; *rp; rp++) {
		sprintf(iiostat_cmd, iiostat_cmd_template,
			(*rp)->pmu_idx, (*rp)->pmu_idx, (*rp)->pmu_idx, (*rp)->pmu_idx);
		ret = parse_events(evl, iiostat_cmd, NULL);
		if (ret)
			goto err;
	}

	evlist__for_each_entry(evl, evsel) {
		evsel->perf_device = list->rps[evsel->idx / metrics_count];
	}
	list->nr_entries = 0;
err:
	iio_root_ports_list_free(list);
	free(iiostat_cmd);
	return ret;
}

int iiostat_parse(const struct option *opt, const char *str,
		  int unset __maybe_unused)
{
	int ret;
	struct iio_root_ports_list *list;
	struct evlist *evl = *(struct evlist **)opt->value;
	struct perf_stat_config *config = (struct perf_stat_config *)opt->data;

	if (evl->core.nr_entries > 0) {
		pr_err("Unsupported event configuration\n");
		return -1;
	}
	config->metric_only = true;
	config->aggr_mode = AGGR_IIO_STACK;
	config->iiostat_run = true;
	ret = iio_root_ports_scan(&list);
	if (ret)
		return ret;

	if (!str) {
		iiostat_mode = IIOSTAT_RUN;
	} else if (!strcmp(str, "show")) {
		iiostat_mode = IIOSTAT_SHOW;
	} else {
		iiostat_mode = IIOSTAT_RUN;
		ret = iio_root_ports_list_filter(&list, str);
		if (ret)
			return ret;
	}
	return iiostat_event_group(evl, list);
}

void iiostat_prefix(struct perf_stat_config *config,
		    struct evlist *evlist,
		    char *prefix, struct timespec *ts)
{
	struct iio_root_port *rp = evlist->selected->perf_device;

	if (rp) {
		if (ts)
			sprintf(prefix, "%6lu.%09lu%s%04x:%02x%s",
					ts->tv_sec, ts->tv_nsec,
					config->csv_sep, rp->domain, rp->bus,
					config->csv_sep);
		else
			sprintf(prefix, "%04x:%02x%s", rp->domain, rp->bus,
				config->csv_sep);
	}
}

void iiostat_print_metric(struct perf_stat_config *config, struct evsel *evsel,
			  struct perf_stat_output_ctx *out)
{
	double iiostat_value = 0;
	u64 prev_count_val = 0;
	const char *iiostat_metric = iiostat_metric_by_idx(evsel->idx);
	u8 die = ((struct iio_root_port *)evsel->perf_device)->die;
	struct perf_counts_values *count = perf_counts(evsel->counts, die, 0);

	if (evsel->prev_raw_counts && !out->force_header) {
		struct perf_counts_values *prev_count = perf_counts(evsel->prev_raw_counts, die, 0);

		prev_count_val = prev_count->val;
		prev_count->val = count->val;
	}
	iiostat_value = (count->val - prev_count_val) / ((double) count->run / count->ena);
	out->print_metric(config, out->ctx, NULL, "%8.0f", iiostat_metric,
			  iiostat_value / (256 * 1024));
}

int iiostat_show_root_ports(struct evlist *evlist, struct perf_stat_config *config)
{
	struct evsel *evsel;
	struct iio_root_port *rp = NULL;
	bool is_show_mode = (iiostat_mode == IIOSTAT_SHOW);

	if (config->aggr_mode != AGGR_IIO_STACK) {
		pr_err("Unsupported event configuration\n");
		return -1;
	}

	if (is_show_mode || verbose) {
		evlist__for_each_entry(evlist, evsel) {
			if (!evsel->perf_device) {
				pr_err("Unsupported event configuration\n");
				return -1;
			}
			if (rp != evsel->perf_device) {
				rp = evsel->perf_device;
				iio_root_port_show(config->output, rp);
			}
		}
	}
	/* Stop iiostat for show mode*/
	config->iiostat_run = !is_show_mode;
	if (is_show_mode)
		iiostat_delete_root_ports(evlist);
	return 0;
}

void iiostat_delete_root_ports(struct evlist *evlist)
{
	struct evsel *evsel;
	struct iio_root_port *rp = NULL;

	evlist__for_each_entry(evlist, evsel) {
		if (rp != evsel->perf_device) {
			rp = evsel->perf_device;
			free(evsel->perf_device);
		}
	}
}
