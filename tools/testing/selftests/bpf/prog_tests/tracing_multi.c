// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "tracing_multi_fentry_test.skel.h"
#include "tracing_multi_fexit_test.skel.h"
#include "tracing_multi_fentry_fexit_test.skel.h"
#include "tracing_multi_fentry_split_test.skel.h"
#include "tracing_multi_fexit_split_test.skel.h"
#include "trace_helpers.h"
#include <bpf/btf.h>

static void multi_fentry_test(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct tracing_multi_fentry_test *skel = NULL;
	int err, prog_fd;

	skel = tracing_multi_fentry_test__open_and_load();
	if (!ASSERT_OK_PTR(skel, "fentry_multi_skel_load"))
		goto cleanup;

	err = tracing_multi_fentry_test__attach(skel);
	if (!ASSERT_OK(err, "fentry_attach"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.test);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");

	ASSERT_EQ(skel->bss->test_result, 8, "test_result");

cleanup:
	tracing_multi_fentry_test__destroy(skel);
}

static void multi_fexit_test(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct tracing_multi_fexit_test *skel = NULL;
	int err, prog_fd;

	skel = tracing_multi_fexit_test__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi_fentry_test__open_and_load"))
		goto cleanup;

	err = tracing_multi_fexit_test__attach(skel);
	if (!ASSERT_OK(err, "tracing_multi_fentry_test__attach"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.test);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");

	ASSERT_EQ(skel->bss->test_arg_result, 8, "test_arg_result");
	ASSERT_EQ(skel->bss->test_ret_result, 8, "test_ret_result");

cleanup:
	tracing_multi_fexit_test__destroy(skel);
}

static void multi_fentry_fexit_test(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct tracing_multi_fentry_fexit_test *skel = NULL;
	int err, prog_fd;

	skel = tracing_multi_fentry_fexit_test__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi_fentry_fexit_test__open_and_load"))
		goto cleanup;

	err = tracing_multi_fentry_fexit_test__attach(skel);
	if (!ASSERT_OK(err, "tracing_multi_fentry_fexit_test__attach"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.test2);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");

	ASSERT_EQ(skel->bss->test1_arg_result, 8, "test1_arg_result");
	ASSERT_EQ(skel->bss->test2_arg_result, 8, "test2_arg_result");
	ASSERT_EQ(skel->bss->test2_ret_result, 8, "test2_ret_result");

cleanup:
	tracing_multi_fentry_fexit_test__destroy(skel);
}

static int resolve_btf_ids(const char **funcs, u32 *ids, unsigned int cnt)
{
	struct btf *btf;
	unsigned int i;

	btf = btf__load_vmlinux_btf();
	if (!btf)
		return -1;

	for (i = 0; i < cnt; i++) {
		ids[i] = (u32) btf__find_by_name_kind(btf, funcs[i], BTF_KIND_FUNC);
		if (!ids[i])
			return -1;
	}
	return 0;
}

static void multi_fentry_split_test(void)
{
	struct tracing_multi_fentry_split_test *skel = NULL;
	LIBBPF_OPTS(bpf_tracing_multi_opts, mopts);
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	u32 ids_prog1[6], ids_prog2[6];
	const char *funcs1[] = {
		"bpf_fentry_test1",
		"bpf_fentry_test2",
		"bpf_fentry_test3",
		"bpf_fentry_test4",
		"bpf_fentry_test5",
		"bpf_fentry_test6",
	};
	const char *funcs2[] = {
		"bpf_fentry_test3",
		"bpf_fentry_test4",
		"bpf_fentry_test5",
		"bpf_fentry_test6",
		"bpf_fentry_test7",
		"bpf_fentry_test8",
	};
	int err, prog_fd;

	skel = tracing_multi_fentry_split_test__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi_fentry_split_test__open_and_load"))
		goto cleanup;

	if (!ASSERT_OK(resolve_btf_ids(funcs1, ids_prog1, 6), "resolve_btf_ids"))
		goto cleanup;

	if (!ASSERT_OK(resolve_btf_ids(funcs2, ids_prog2, 6), "resolve_btf_ids"))
		goto cleanup;

	mopts.btf_ids = ids_prog1;
	mopts.cnt = 6;

	skel->links.prog1 = bpf_program__attach_tracing_multi(skel->progs.prog1, NULL, &mopts);
	if (!ASSERT_OK(libbpf_get_error(skel->links.prog1), "bpf_program__attach_tracing_multi"))
                goto cleanup;

	mopts.btf_ids = ids_prog2;
	mopts.cnt = 6;

	skel->links.prog2 = bpf_program__attach_tracing_multi(skel->progs.prog2, NULL, &mopts);
	if (!ASSERT_OK(libbpf_get_error(skel->links.prog2), "bpf_program__attach_tracing_multi"))
                goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.prog2);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");

	ASSERT_EQ(skel->bss->test1_result, 6, "test1_result");
	ASSERT_EQ(skel->bss->test2_result, 6, "test2_result");

cleanup:
	tracing_multi_fentry_split_test__destroy(skel);
}

static void multi_fexit_split_test(void)
{
	struct tracing_multi_fexit_split_test *skel = NULL;
	LIBBPF_OPTS(bpf_tracing_multi_opts, mopts);
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	u32 ids_prog1[6], ids_prog2[6];
	const char *funcs1[] = {
		"bpf_fentry_test1",
		"bpf_fentry_test2",
		"bpf_fentry_test3",
		"bpf_fentry_test4",
		"bpf_fentry_test5",
		"bpf_fentry_test6",
	};
	const char *funcs2[] = {
		"bpf_fentry_test3",
		"bpf_fentry_test4",
		"bpf_fentry_test5",
		"bpf_fentry_test6",
		"bpf_fentry_test7",
		"bpf_fentry_test8",
	};
	int err, prog_fd;

	skel = tracing_multi_fexit_split_test__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi_fexit_split_test__open_and_load"))
		goto cleanup;

	if (!ASSERT_OK(resolve_btf_ids(funcs1, ids_prog1, 6), "resolve_btf_ids"))
		goto cleanup;

	if (!ASSERT_OK(resolve_btf_ids(funcs2, ids_prog2, 6), "resolve_btf_ids"))
		goto cleanup;

	mopts.btf_ids = ids_prog1;
	mopts.cnt = 6;

	skel->links.prog1 = bpf_program__attach_tracing_multi(skel->progs.prog1, NULL, &mopts);
	if (!ASSERT_OK(libbpf_get_error(skel->links.prog1), "bpf_program__attach_tracing_multi"))
                goto cleanup;

	mopts.btf_ids = ids_prog2;
	mopts.cnt = 6;

	skel->links.prog2 = bpf_program__attach_tracing_multi(skel->progs.prog2, NULL, &mopts);
	if (!ASSERT_OK(libbpf_get_error(skel->links.prog2), "bpf_program__attach_tracing_multi"))
                goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.prog2);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");

	ASSERT_EQ(skel->bss->test1_result, 6, "test1_result");
	ASSERT_EQ(skel->bss->test2_result, 6, "test2_result");

cleanup:
	tracing_multi_fexit_split_test__destroy(skel);
}

void test_tracing_multi_test(void)
{
	if (test__start_subtest("fentry"))
		multi_fentry_test();
	if (test__start_subtest("fexit"))
		multi_fexit_test();
	if (test__start_subtest("fentry_fexit"))
		multi_fentry_fexit_test();
	if (test__start_subtest("fentry_split"))
		multi_fentry_split_test();
	if (test__start_subtest("fexit_split"))
		multi_fexit_split_test();
}
