// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "trace_helpers.h"
#include "krava.skel.h"

void test_krava(void)
{
	struct bpf_link *link;
	struct krava *skel;
	int i;

	skel = krava__open_and_load();
	if (!ASSERT_OK_PTR(skel, "krava__open_and_load"))
		goto cleanup;

	for (i = 0; i < 300; i++) {
		link = bpf_program__attach_trace(skel->progs.test);
		if (!ASSERT_OK_PTR(link, "bpf_program__attach_kprobe_multi_opts"))
			goto cleanup;
		bpf_link__destroy(link);
	}

cleanup:
	krava__destroy(skel);
}
