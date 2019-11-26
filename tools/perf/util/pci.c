// SPDX-License-Identifier: GPL-2.0
/*
 * Helper functions to access PCI CFG space.
 *
 * Copyright (C) 2019, Intel Corporation
 *
 * Authors: Roman Sudarikov <roman.sudarikov@intel.com>
 *	    Alexander Antonov <alexander.antonov@intel.com>
 */
#include "pci.h"
#ifdef HAVE_LIBPCI_SUPPORT
#include <pci/pci.h>
#endif
#include <api/fs/fs.h>
#include <linux/kernel.h>
#include <string.h>
#include <unistd.h>

#define PCI_DEVICE_PATH_TEMPLATE "bus/pci/devices/0000:%02x:%02x.0"
#define PCI_DEVICE_FILE_TEMPLATE PCI_DEVICE_PATH_TEMPLATE"/%s"

#ifdef HAVE_LIBPCI_SUPPORT
static struct pci_access *pacc;
#endif

void pci_library_init(void)
{
#ifdef HAVE_LIBPCI_SUPPORT
	pacc = pci_alloc();
	if (pacc) {
		pci_init(pacc);
		pci_scan_bus(pacc);
	}
#endif
}

void pci_library_cleanup(void)
{
#ifdef HAVE_LIBPCI_SUPPORT
	pci_cleanup(pacc);
#endif
}

char *pci_device_name(struct bdf bdf __maybe_unused)
{
#ifdef HAVE_LIBPCI_SUPPORT
	struct pci_dev *device;
	char namebuf[PATH_MAX];

	if (pacc) {
		device = pci_get_dev(pacc, 0, bdf.busno, bdf.devno, bdf.funcno);
		if (device) {
			pci_fill_info(device, PCI_FILL_IDENT);
			return pci_lookup_name(pacc, namebuf, sizeof(namebuf),
					       PCI_LOOKUP_DEVICE, device->vendor_id,
					       device->device_id);
		}
	}
	return (char *)"";
#else
	return (char *)"";
#endif
}

static bool directory_exists(const char * const path)
{
	return (access(path, F_OK) == 0);
}

bool pci_device_probe(struct bdf bdf)
{
	char path[PATH_MAX];

	scnprintf(path, PATH_MAX, "%s/"PCI_DEVICE_PATH_TEMPLATE,
		  sysfs__mountpoint(), bdf.busno, bdf.devno);
	return directory_exists(path);
}

bool is_pci_device_root_port(struct bdf bdf, u8 *secondary, u8 *subordinate)
{
	char path[PATH_MAX];
	int secondary_interim;
	int subordinate_interim;

	scnprintf(path, PATH_MAX, PCI_DEVICE_FILE_TEMPLATE,
		  bdf.busno, bdf.devno, "secondary_bus_number");
	if (!sysfs__read_int(path, &secondary_interim)) {
		scnprintf(path, PATH_MAX, PCI_DEVICE_FILE_TEMPLATE,
			  bdf.busno, bdf.devno, "subordinate_bus_number");
		if (!sysfs__read_int(path, &subordinate_interim)) {
			if (secondary)
				*secondary = (u8)secondary_interim;
			if (subordinate)
				*subordinate = (u8)subordinate_interim;
			return true;
		}
	}
	return false;
}
