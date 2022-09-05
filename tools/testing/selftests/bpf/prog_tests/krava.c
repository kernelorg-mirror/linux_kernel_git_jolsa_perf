// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "krava.skel.h"

void test_krava(void)
{
	LIBBPF_OPTS(bpf_prog_load_opts, trace_opts,
		.expected_attach_type = BPF_TRACE_FENTRY,
	);
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct krava *skel = NULL;
	int err, prog_fd;

	skel = krava__open();
	if (!ASSERT_OK_PTR(skel, "krava__open"))
		return;

	err = krava__load(skel);
	if (!ASSERT_OK(err, "krava__load"))
		goto cleanup;

	err = krava__attach(skel);
	if (!ASSERT_OK(err, "krava__attach"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.prog);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "test_run");

cleanup:
	krava__destroy(skel);
}
