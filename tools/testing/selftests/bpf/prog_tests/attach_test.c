// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <test_progs.h>
#include "attach_test.skel.h"

void test_attach_test(void)
{
	struct attach_test *attach_skel = NULL;
	__u32 duration = 0;
	int err;

	attach_skel = attach_test__open_and_load();
	if (CHECK(!attach_skel, "attach_skel_load", "attach skeleton failed\n"))
		goto cleanup;

	err = attach_test__attach(attach_skel);
	if (CHECK(err, "attach", "attach failed: %d\n", err))
		goto cleanup;

cleanup:
	attach_test__destroy(attach_skel);
}
