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

struct dev_info {
	struct bdf bdf;
	u8 ch_mask;
	u8 socket;
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
			device->dev_info.socket,
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
