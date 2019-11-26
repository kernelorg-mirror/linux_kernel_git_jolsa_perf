// SPDX-License-Identifier: GPL-2.0
#include "pci/pci.h"

int main(void)
{
	struct pci_access *pacc = pci_alloc();

	pci_cleanup(pacc);
	return 0;
}
