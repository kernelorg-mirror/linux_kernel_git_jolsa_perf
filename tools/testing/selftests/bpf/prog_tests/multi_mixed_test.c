// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "multi_mixed_test.skel.h"

void test_multi_mixed_test(void)
{
	struct bpf_link *link1, *link2, *linkm1, *linkm2;
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct multi_mixed_test *skel = NULL;
	int err, prog_fd;

	skel = multi_mixed_test__open_and_load();
	if (!ASSERT_OK_PTR(skel, "fentry_multi_skel_load"))
		goto cleanup;

	link1 = bpf_program__attach_trace(skel->progs.test1);
	if (!ASSERT_OK_PTR(link1, "attach_trace"))
		goto cleanup;

	link2 = bpf_program__attach_trace(skel->progs.test2);
	if (!ASSERT_OK_PTR(link2, "attach_trace"))
		goto cleanup;

	linkm1 = bpf_program__attach_tracing_multi(skel->progs.test3,
						  "bpf_fentry_test*", NULL);
	if (!ASSERT_OK_PTR(linkm1, "attach_trace"))
		goto cleanup;

	linkm2 = bpf_program__attach_tracing_multi(skel->progs.test4,
						  "bpf_fentry_test*", NULL);
	if (!ASSERT_OK_PTR(linkm2, "attach_trace"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.test1);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");

	ASSERT_EQ(skel->bss->test1_result, 1, "test1_result");
	ASSERT_EQ(skel->bss->test2_result, 1, "test2_result");
	ASSERT_EQ(skel->bss->test3_arg_result, 8, "test3_arg_result");
	ASSERT_EQ(skel->bss->test4_arg_result, 8, "test4_arg_result");
	ASSERT_EQ(skel->bss->test4_ret_result, 8, "test4_ret_result");

cleanup:
	bpf_link__destroy(link1);
	bpf_link__destroy(link2);
	bpf_link__destroy(linkm1);
	bpf_link__destroy(linkm2);
	multi_mixed_test__destroy(skel);
}
