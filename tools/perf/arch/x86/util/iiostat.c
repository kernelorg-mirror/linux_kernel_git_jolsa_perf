// SPDX-License-Identifier: GPL-2.0
/*
 * perf stat --iiostat
 *
 * Copyright (C) 2019, Intel Corporation
 *
 * Authors: Roman Sudarikov <roman.sudarikov@intel.com>
 *	    Alexander Antonov <alexander.antonov@intel.com>
 */
#include "path.h"
#include "pci.h"
#include "util/cpumap.h"
#include <api/fs/fs.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include "util/debug.h"
#include "util/iiostat.h"
#include "util/counts.h"
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

/*
 * The Intel® Xeon® Scalable processor family (code name Skylake-SP) makes
 * significant changes in integrated I/O (IIO) architecture. The new solution
 * introduces IIO stacks which are responsible for managing traffic between PCIe
 * domain and the Mesh domain. Each IIO stack has its own PMON block and can
 * handle either DMI port, x16 PCIe root port, MCP-Link or various built-in
 * accelerators. IIO PMON blocks allow concurrent monitoring of I/O flows up
 * to 4 x4 bifurcation within each IIO stack.
 *
 * New --iiostat mode in perf stat is intended to provide four I/O performance
 * metrics per each IO device below IIO stacks:
 *     --Inbound Read(Mb)   - I/O device reads from the host memory, in Mb
 *     --Inbound Write(Mb)  - I/O device writes to the host memory, in Mb
 *     --Outbound Read(Mb)  - CPU reads from I/O device, in Mb
 *     --Outbound Write(Mb) - CPU writes to I/O device, in Mb
 *
 * Each metric requiries only one IIO event which increments at every 4B
 * transfer in corresponding direction. The formulas to compute metrics
 * are generic:
 *     #EventCount * 4B / (1024 * 1024)
 *
 * This implementation starts from discovering IIO stacks on the platform and
 * all devices below each stack. Next step is to configure group of four events
 * per each device and tie each event group to its device.
 *
 * Sample output:

./perf stat --iiostat=show
	S0-RootPort0-uncore_iio_0<00:00.0>
	S1-RootPort0-uncore_iio_0<81:00.0>
	S0-RootPort1-uncore_iio_1<18:00.0>
	S1-RootPort1-uncore_iio_1<86:00.0>
	S1-RootPort1-uncore_iio_1<88:00.0>
	S0-RootPort2-uncore_iio_2<3d:00.0>
	S1-RootPort2-uncore_iio_2<af:00.0>
	S1-RootPort3-uncore_iio_3<da:00.0>

./perf stat --iiostat=af:00.0 -- dd if=/dev/zero of=/dev/nvme0n1 bs=1M oflag=direct
	381555+0 records in
	381554+0 records out
	400088457216 bytes (400 GB, 373 GiB) copied, 374.044 s, 1.1 GB/s

Performance counter stats for 'system wide':

	 device  Inbound Read(MB)  Inbound Write(MB)  Outbound Read(MB)  Outbound Write(MB)
	af:00.0    382462                47                   0                 23

374.045775505 seconds time elapsed
 */
#define PCI_BUS_MAX_DEVICE_NUMBER 32
#define PCI_BUS_MAX_FUNCTION_NUMBER 8

#define PLATFORM_MAPPING_PATH	"devices/uncore_iio_%d/platform_mapping"

typedef enum {
	IIOSTAT_NONE		= 0,
	IIOSTAT_SHOW		= 1,
	IIOSTAT_RUN		= 2
} iiostat_mode_t;

static iiostat_mode_t iiostat_mode = IIOSTAT_NONE;

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

static const char *get_iiostat_metric(int idx)
{
	return *(iiostat_metrics + idx % iiostat_metrics_count());
}

struct dev_info {
	struct bdf bdf;
	u8 ch_mask;
	u8 die;
	u8 pmu_idx;
	u8 root_port_nr;
};

struct iio_device {
	struct list_head node;
	struct dev_info	dev_info;
	int idx;
};

struct iio_devs_list {
	struct list_head devices;
	int nr_entries;
};

/**
 * __iio_devs_for_each_device - iterate thru all the iio devices
 * @list: list_head instance to iterate
 * @device: struct iio_device iterator
 */
#define __iio_devs_for_each_device(list, device) \
		list_for_each_entry(device, list, node)

/**
 * iio_devs_list_for_each_device - iterate thru all the iio devices
 * @list: iio_devs_list instance to iterate
 * @device: struct iio_device iterator
 */
#define iio_devs_list_for_each_device(list, device) \
	__iio_devs_for_each_device(&(list->devices), device)

/**
 * __iio_devs_for_each_device_safe - safely iterate thru all the iio devices
 * @devices: list_head instance to iterate
 * @tmp: struct iio_device temp iterator
 * @device: struct iio_device iterator
 */
#define __iio_devs_for_each_device_safe(devices, tmp, device) \
		list_for_each_entry_safe(device, tmp, devices, node)

/**
 * iio_devs_list_for_each_device_safe - safely iterate thru all the iio devices
 * @list: iio_devs_list instance to iterate
 * @tmp: struct iio_device temp iterator
 * @device: struct iio_device iterator
 */
#define iio_devs_list_for_each_device_safe(list, tmp, device) \
		__iio_devs_for_each_device_safe(&(list->devices), tmp, device)

#define iio_device_delete_from_list(device) \
		list_del(&(device->node))

static u8 *rp_nr;

static u8 get_rp_nr(u8 die)
{
	return rp_nr[die]++;
}

static u64 platform_mapping_build(char *buf)
{
	char *end;
	u8 offset = 0;
	unsigned long long interim = 0;

	for (long mapping_byte = strtol(buf, &end, 16);
		buf != end; mapping_byte = strtol(buf, &end, 16)) {
		buf = end + 1;
		if (*end == ',' || *end == '\n')
			interim |= (u64)mapping_byte << (8 * offset++);
	}
	return interim;
}

static int uncore_pmu_iio_platform_mapping_read(u8 pmu_idx, u64 * const mapping)
{
	char *buf;
	char path[PATH_MAX];
	size_t size;

	scnprintf(path, PATH_MAX, PLATFORM_MAPPING_PATH, pmu_idx);
	if (sysfs__read_str(path, &buf, &size) < 0) {
		fprintf(stderr, "iiostat is not supported\n");
		return -1;
	}
	*mapping = (size == 2) ? 0 : platform_mapping_build(buf);
	free(buf);

	return 0;
}

static struct iio_device *iio_device_new(struct dev_info *info)
{
	struct iio_device *p =
		(struct iio_device *)calloc(1, sizeof(struct iio_device));

	if (p) {
		INIT_LIST_HEAD(&(p->node));
		p->dev_info = *info;
		p->idx = -1;
	}
	return p;
}

static void iio_device_delete(struct iio_device *device)
{
	if (device) {
		list_del_init(&(device->node));
		free(device);
	}
}

static void iiostat_device_show(FILE *output,
			const struct iio_device * const device)
{
	if (output && device)
		fprintf(output, "S%d-RootPort%d-uncore_iio_%d<%02x:%02x.%x>\n",
			device->dev_info.die,
			device->dev_info.root_port_nr, device->dev_info.pmu_idx,
			device->dev_info.bdf.busno, device->dev_info.bdf.devno,
			device->dev_info.bdf.funcno);
}

static struct iio_devs_list *iio_devs_list_new(void)
{
	struct iio_devs_list *devs_list =
		(struct iio_devs_list *)calloc(1, sizeof(struct iio_devs_list));

	if (devs_list)
		INIT_LIST_HEAD(&(devs_list->devices));
	return devs_list;
}

static void iio_devs_list_free(struct iio_devs_list *list)
{
	struct iio_device *tmp_device;
	struct iio_device *device;

	if (list) {
		iio_devs_list_for_each_device_safe(list, tmp_device, device)
			iio_device_delete(device);
		list_del_init(&(list->devices));
		free(list);
	}
}

static bool is_same_iio_device(struct bdf lhd, struct bdf rhd)
{
	return (lhd.busno == rhd.busno) && (lhd.devno == rhd.devno) &&
		(lhd.funcno == rhd.funcno);
}

static void iio_devs_list_add_device(struct iio_devs_list *list,
				      struct iio_device * const device)
{
	struct iio_device *it;

	if (list && device) {
		iio_devs_list_for_each_device(list, it)
			if (is_same_iio_device(it->dev_info.bdf, device->dev_info.bdf))
				return;
		device->idx = list->nr_entries++;
		list_add_tail(&(device->node), &(list->devices));
	}
}

static void iio_devs_list_join_list(struct iio_devs_list *dest,
				     struct iio_devs_list *src)
{
	int idx = 0;
	struct iio_device *it;

	if (dest && src) {
		if (dest->nr_entries) {
			it = list_last_entry(&(dest->devices),
					     struct iio_device, node);
			idx = it->idx + 1;
		}
		iio_devs_list_for_each_device(src, it)
			it->idx = idx++;
		list_splice_tail(&(src->devices), &(dest->devices));
		dest->nr_entries += src->nr_entries;
	}
}

static int pci_rp_probe(struct dev_info *info, struct iio_devs_list *list)
{
	u8 secondary_bus_number = 0;
	u8 subordinate_bus_number = 0;
	struct iio_device *device = NULL;

	if (!pci_device_probe(info->bdf))
		return 0;

	if (!is_pci_device_root_port(info->bdf,
				     &secondary_bus_number,
				     &subordinate_bus_number)) {
		secondary_bus_number = info->bdf.busno;
		subordinate_bus_number = info->bdf.busno;
	}

	for (u8 b = secondary_bus_number; b <= subordinate_bus_number; b++) {
		for (u8 d = 0; d < PCI_BUS_MAX_DEVICE_NUMBER; d++) {
			for (u8 f = 0; f < PCI_BUS_MAX_FUNCTION_NUMBER; f++) {
				info->bdf.busno = b;
				info->bdf.devno = d;
				info->bdf.funcno = f;
				if (!pci_device_probe(info->bdf) ||
				    is_pci_device_root_port(info->bdf, NULL, NULL))
					continue;
				device = iio_device_new(info);
				if (device) {
					iio_devs_list_add_device(list, device);
					break;
				}
				return -ENOMEM;
			}
		}
	}
	return 0;
}

static int pci_rp_scan(u8 rp, u8 pmu_idx, u8 die,
			struct iio_devs_list **list)
{
	int ret = 0;
	u8 part = 0;
	struct dev_info info;
	struct iio_device *device = NULL;

	struct iio_devs_list *interim_list = iio_devs_list_new();

	if (!interim_list)
		return -ENOMEM;

	info.bdf.busno = rp;
	info.bdf.funcno = 0;
	info.pmu_idx = pmu_idx;
	info.die = die;
	info.root_port_nr = get_rp_nr(die);

	/* Extra case for root port 0x00*/
	if (info.bdf.busno == 0x00) {
		info.bdf.devno = 0;
		device = iio_device_new(&info);
		if (device)
			iio_devs_list_add_device(interim_list, device);
		else {
			iio_devs_list_free(interim_list);
			return -ENOMEM;
		}
	} else {
		for (part = 0; part < 4; part++) {
			info.bdf.devno = part;
			info.ch_mask = (1 << part);
			ret = pci_rp_probe(&info, interim_list);
			if (ret) {
				iio_devs_list_free(interim_list);
				return ret;
			}
		}
	}

	if (interim_list->nr_entries)
		*list = interim_list;
	else
		iio_devs_list_free(interim_list);

	return 0;
}

static int pmu_scan(u8 pmu_idx, u64 mapping, struct iio_devs_list **list)
{
	int ret;
	u8 rp;
	struct iio_devs_list *interim_list, *rp_list = NULL;

	interim_list = iio_devs_list_new();
	if (!interim_list)
		return -ENOMEM;

	for (u8 die = 0; die < cpu__max_node(); die++) {
		rp = (u8)(mapping >> (die * 8));
		if (!rp && die)
			break;

		ret = pci_rp_scan(rp, pmu_idx, die, &rp_list);
		if (ret) {
			iio_devs_list_free(interim_list);
			return ret;
		}
		if (rp_list) {
			iio_devs_list_join_list(interim_list, rp_list);
			free(rp_list);
			rp_list = NULL;
		}
	}
	if (interim_list->nr_entries)
		*list = interim_list;
	else
		iio_devs_list_free(interim_list);
	return 0;
}

static int iio_devs_scan(struct iio_devs_list **list)
{
	u64 mapping;
	int ret;
	struct iio_devs_list *pmu_dev_list = NULL;
	struct iio_devs_list *interim = NULL;

	rp_nr = (u8 *)calloc(cpu__max_node(), 1);
	if (!rp_nr)
		return -ENOMEM;

	interim = iio_devs_list_new();
	if (!interim) {
		free(rp_nr);
		return -ENOMEM;
	}

	for (u8 pmu_idx = 0; pmu_idx < 6; pmu_idx++) {
		ret = uncore_pmu_iio_platform_mapping_read(pmu_idx, &mapping);
		if (ret)
			break;
		/* IIO stack 0 on die 0 is always on bus 0x00.*/
		if ((mapping == 0) && pmu_idx)
			continue;

		ret = pmu_scan(pmu_idx, mapping, &pmu_dev_list);
		if (ret)
			break;

		if (pmu_dev_list) {
			iio_devs_list_join_list(interim, pmu_dev_list);
			free(pmu_dev_list);
			pmu_dev_list = NULL;
		}
	}

	if (!ret)
		*list = interim;
	else
		iio_devs_list_free(interim);

	free(rp_nr);

	return ret;
}

static int iio_dev_parse_bdf_str(struct bdf *bdf, char *str)
{
	int ret = 0;
	regex_t regex;
	/*
	 * Expected format bus:device.function:
	 * Valid bus range [0:ff]
	 * Valid device range [0:1f]
	 * Valid function range [0:7]
	 * Example: af:00.0, d:0.0, 5e:0.0
	 */
	regcomp(&regex,
		"^([a-f0-9A-F]{1,2}):(([0|1]{0,1})([0-9a-fA-F]{1})).([0-7]{1})$",
		REG_EXTENDED);
	ret = regexec(&regex, str, 0, NULL, 0);
	if (!ret)
		sscanf(str, "%02hhx:%02hhx.%hhx",
		       &bdf->busno, &bdf->devno, &bdf->funcno);
	else
		pr_warning("Unrecognized device format: %s\n", str);

	regfree(&regex);
	return ret;
}

static struct iio_device *pci_devs_list_find_device_by_bdf(
			   const struct iio_devs_list * const list,
			   struct bdf bdf)
{
	struct iio_device *it;

	if (list) {
		iio_devs_list_for_each_device(list, it) {
			if (is_same_iio_device(it->dev_info.bdf, bdf))
				return it;
		}
	}
	return NULL;
}

static int iio_devs_list_filter_by_bdf(struct iio_devs_list **list,
					const char *bdf_str)
{
	struct bdf bdf;
	struct iio_device *device;
	const char *delim = ",";
	char *token = NULL;
	char *tmp;
	char *tmp_bdf_str = (char *)bdf_str;

	struct iio_devs_list *filtered_list = iio_devs_list_new();

	if (!filtered_list)
		return -ENOMEM;

	token = strtok(tmp_bdf_str, delim);
	while (token != NULL) {
		tmp = token;
		if (!iio_dev_parse_bdf_str(&bdf, tmp)) {
			if (!pci_devs_list_find_device_by_bdf(filtered_list, bdf)) {
				device = pci_devs_list_find_device_by_bdf(*list, bdf);
				if (device) {
					iio_device_delete_from_list(device);
					iio_devs_list_add_device(filtered_list, device);
				} else
					pr_warning("Device %02x:%02x.%x not found\n",
						   bdf.busno, bdf.devno, bdf.funcno);
			}
		}
		token = strtok(NULL, delim);
	}
	iio_devs_list_free(*list);
	*list = filtered_list;
	return 0;
}

static struct iio_device *iio_dev_get_by_idx(const struct iio_devs_list *list,
					      int idx)
{
	struct iio_device *device = NULL;

	if (idx < list->nr_entries)
		iio_devs_list_for_each_device(list, device)
			if (device->idx == idx)
				break;

	return device;
}

static int iiostat_event_group(struct evlist *evl,
				struct iio_devs_list *dev_list)
{
	int ret = 0;
	struct iio_device *device = NULL;
	const char *iiostat_cmd_template =
	"{uncore_iio_%x/event=0x83,umask=0x04,ch_mask=0x%02x,fc_mask=0x07/,\
	uncore_iio_%x/event=0x83,umask=0x01,ch_mask=0x%02x,fc_mask=0x07/,\
	uncore_iio_%x/event=0xc0,umask=0x04,ch_mask=0x%02x,fc_mask=0x07/,\
	uncore_iio_%x/event=0xc0,umask=0x01,ch_mask=0x%02x,fc_mask=0x07/}";
	const int len_template = strlen(iiostat_cmd_template) + 1;
	struct evsel *evsel = NULL;
	int metrics_count = iiostat_metrics_count();
	char *iiostat_cmd = calloc(len_template, 1);

	if (!iiostat_cmd)
		return -ENOMEM;
	iio_devs_list_for_each_device(dev_list, device) {
		sprintf(iiostat_cmd, iiostat_cmd_template,
			device->dev_info.pmu_idx, device->dev_info.ch_mask,
			device->dev_info.pmu_idx, device->dev_info.ch_mask,
			device->dev_info.pmu_idx, device->dev_info.ch_mask,
			device->dev_info.pmu_idx, device->dev_info.ch_mask);
		ret = parse_events(evl, iiostat_cmd, NULL);
		if (ret)
			goto out;
	}
	evlist__for_each_entry(evl, evsel)
		evsel->perf_device = iio_dev_get_by_idx(dev_list,
							evsel->idx / metrics_count);
out:
	list_del_init(&(dev_list->devices));
	iio_devs_list_free(dev_list);
	free(iiostat_cmd);
	return ret;
}

int iiostat_parse(const struct option *opt,
		  const char *str,
		  int unset __maybe_unused)
{
	int ret = 0;
	struct iio_devs_list *dev_list = NULL;
	struct evlist *evl = *(struct evlist **)opt->value;
	struct perf_stat_config *config = (struct perf_stat_config *)opt->data;

	if (evl->core.nr_entries > 0) {
		pr_err("unsupported event configuration\n");
		return -1;
	}
	config->metric_only = true;
	config->aggr_mode = AGGR_DEVICE;
	config->iiostat_run = true;
	ret = iio_devs_scan(&dev_list);
	if (ret)
		return ret;

	if (!str)
		iiostat_mode = IIOSTAT_RUN;
	else if (!strcmp(str, "show"))
		iiostat_mode = IIOSTAT_SHOW;
	else {
		iiostat_mode = IIOSTAT_RUN;
		ret = iio_devs_list_filter_by_bdf(&dev_list, str);
		if (ret) {
			iio_devs_list_free(dev_list);
			return ret;
		}
		if (dev_list->nr_entries == 0) {
			pr_err("Requested devices were not found\n");
			iio_devs_list_free(dev_list);
			return -1;
		}
	}
	return iiostat_event_group(evl, dev_list);
}

void iiostat_prefix(struct perf_stat_config *config,
		    struct evlist *evlist,
		    char *prefix, struct timespec *ts)
{
	struct iio_device *device = evlist->selected->perf_device;

	if (device) {
		if (ts)
			sprintf(prefix, "%6lu.%09lu%s%02x:%02x.%x%s",
					ts->tv_sec, ts->tv_nsec,
					config->csv_sep, device->dev_info.bdf.busno,
					device->dev_info.bdf.devno, device->dev_info.bdf.funcno,
					config->csv_sep);
		else
			sprintf(prefix, "%02x:%02x.%x%s",
					device->dev_info.bdf.busno, device->dev_info.bdf.devno,
					device->dev_info.bdf.funcno, config->csv_sep);
	}
}

void iiostat_print_metric(struct perf_stat_config *config, struct evsel *evsel,
			  struct perf_stat_output_ctx *out)
{
	double iiostat_value = 0;
	u64 prev_count_val = 0;
	const char *iiostat_metric = get_iiostat_metric(evsel->idx);
	u8 device_die =
		((struct iio_device *)evsel->perf_device)->dev_info.die;
	struct perf_counts_values *count =
		perf_counts(evsel->counts, device_die, 0);

	if (evsel->prev_raw_counts && !out->force_header) {
		struct perf_counts_values *prev_count =
			perf_counts(evsel->prev_raw_counts, device_die, 0);
		prev_count_val = prev_count->val;
		prev_count->val = count->val;
	}
	iiostat_value = (count->val - prev_count_val) / ((double) count->run / count->ena);
	out->print_metric(config, out->ctx, NULL, "%8.0f",
			  iiostat_metric, iiostat_value / (256 * 1024));
}

int iiostat_print_device_list(struct evlist *evlist,
			       struct perf_stat_config *config)
{
	struct evsel *evsel;
	struct iio_device *device = NULL;

	if (config->aggr_mode != AGGR_DEVICE) {
		pr_err("unsupported event config\n");
		return -1;
	}

	evlist__for_each_entry(evlist, evsel) {
		if (!evsel->perf_device) {
			pr_err("unsupported event config\n");
			return -1;
		}
		if ((iiostat_mode == IIOSTAT_SHOW || verbose) && device != evsel->perf_device) {
			device = evsel->perf_device;
			iiostat_device_show(config->output, device);
		}
	}
	return (iiostat_mode == IIOSTAT_SHOW) ? -1 : 0;
}

void iiostat_delete_device_list(struct evlist *evlist)
{
	struct evsel *evsel;
	struct iio_device *device = NULL;

	evlist__for_each_entry(evlist, evsel) {
		if (device != evsel->perf_device) {
			device = evsel->perf_device;
			iio_device_delete(evsel->perf_device);
		}
	}
}
