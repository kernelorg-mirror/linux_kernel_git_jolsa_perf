// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>
#include "rawtp_contention.skel.h"

void test_rawtp_contention(void)
{
	struct rawtp_contention *skel = NULL;
	int err;

	skel = rawtp_contention__open_and_load();
	if (!ASSERT_OK_PTR(skel, "rawtp_contention__open_and_load"))
		return;

	err = rawtp_contention__attach(skel);
	if (!ASSERT_OK(err, "rawtp_contention__attach"))
		goto cleanup;

	getchar();

cleanup:
	rawtp_contention__destroy(skel);
}
