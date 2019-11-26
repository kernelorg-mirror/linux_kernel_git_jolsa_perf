/* SPDX-License-Identifier: GPL-2.0*/
/*
 *
 * Copyright (C) 2019, Intel Corporation
 *
 * Authors: Roman Sudarikov <roman.sudarikov@intel.com>
 *	    Alexander Antonov <alexander.antonov@intel.com>
 */
#ifndef _PCI_H
#define _PCI_H

#include <linux/types.h>

struct bdf {
	u8 busno;
	u8 devno;
	u8 funcno;
};

bool pci_device_probe(struct bdf bdf);
bool is_pci_device_root_port(struct bdf bdf, u8 *secondary, u8 *subordinate);

#endif /* _PCI_H */
