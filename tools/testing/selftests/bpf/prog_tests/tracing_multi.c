// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "tracing_multi_fentry_test.skel.h"
#include "tracing_multi_fexit_test.skel.h"
#include "tracing_multi_fentry_fexit_test.skel.h"
#include "tracing_multi_fentry_split_test.skel.h"
#include "tracing_multi_fexit_split_test.skel.h"
#include "tracing_multi_single_test.skel.h"
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

static void multi_fentry_split_test(void)
{
	struct tracing_multi_fentry_split_test *skel = NULL;
	LIBBPF_OPTS(bpf_tracing_multi_opts, mopts);
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	u32 ids_prog1[6], ids_prog2[6];
	int err, prog_fd;
	struct btf *btf;

	btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(btf, "btf__load_vmlinux_btf"))
		return;

	skel = tracing_multi_fentry_split_test__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi_fentry_split_test__open_and_load"))
		goto cleanup;

#define GET_ID(__sym, __id)						\
	__id = (u32) btf__find_by_name_kind(btf, __sym, BTF_KIND_FUNC);	\
	if (!ASSERT_GT((s32) __id, 0, "btf__find_by_name_kind"))	\
		goto cleanup;

	GET_ID("bpf_fentry_test1", ids_prog1[0])
	GET_ID("bpf_fentry_test2", ids_prog1[1])
	GET_ID("bpf_fentry_test3", ids_prog1[2])
	GET_ID("bpf_fentry_test4", ids_prog1[3])
	GET_ID("bpf_fentry_test5", ids_prog1[4])
	GET_ID("bpf_fentry_test6", ids_prog1[5])

	GET_ID("bpf_fentry_test3", ids_prog2[0])
	GET_ID("bpf_fentry_test4", ids_prog2[1])
	GET_ID("bpf_fentry_test5", ids_prog2[2])
	GET_ID("bpf_fentry_test6", ids_prog2[3])
	GET_ID("bpf_fentry_test7", ids_prog2[4])
	GET_ID("bpf_fentry_test8", ids_prog2[5])

#undef GET_ID

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
	int err, prog_fd;
	struct btf *btf;

	btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(btf, "btf__load_vmlinux_btf"))
		return;

	skel = tracing_multi_fexit_split_test__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi_fexit_split_test__open_and_load"))
		goto cleanup;

#define GET_ID(__sym, __id)						\
	__id = (u32) btf__find_by_name_kind(btf, __sym, BTF_KIND_FUNC);	\
	if (!ASSERT_GT((s32) __id, 0, "btf__find_by_name_kind"))	\
		goto cleanup;

	GET_ID("bpf_fentry_test1", ids_prog1[0])
	GET_ID("bpf_fentry_test2", ids_prog1[1])
	GET_ID("bpf_fentry_test3", ids_prog1[2])
	GET_ID("bpf_fentry_test4", ids_prog1[3])
	GET_ID("bpf_fentry_test5", ids_prog1[4])
	GET_ID("bpf_fentry_test6", ids_prog1[5])

	GET_ID("bpf_fentry_test3", ids_prog2[0])
	GET_ID("bpf_fentry_test4", ids_prog2[1])
	GET_ID("bpf_fentry_test5", ids_prog2[2])
	GET_ID("bpf_fentry_test6", ids_prog2[3])
	GET_ID("bpf_fentry_test7", ids_prog2[4])
	GET_ID("bpf_fentry_test8", ids_prog2[5])

#undef GET_ID

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

/* single links first, then multi link */
static void multi_single_1_test(void)
{
	LIBBPF_OPTS(bpf_tracing_multi_opts, mopts);
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct tracing_multi_single_test *skel = NULL;
	int err, prog_fd;

	skel = tracing_multi_single_test__open_and_load();
	if (!ASSERT_OK_PTR(skel, "fentry_multi_skel_load"))
		goto cleanup;

	skel->links.prog3 = bpf_program__attach_trace(skel->progs.prog3);
	if (!ASSERT_OK_PTR(link, "attach_fentry"))
		goto cleanup;

	skel->links.prog4 = bpf_program__attach_trace(skel->progs.prog4);
	if (!ASSERT_OK_PTR(link, "attach_fentry"))
		goto cleanup;

	skel->links.prog5 = bpf_program__attach_trace(skel->progs.prog5);
	if (!ASSERT_OK_PTR(link, "attach_fentry"))
		goto cleanup;

	skel->links.fentry_multi = bpf_program__attach_tracing_multi(skel->progs.fentry_multi, "bpf_fentry_test*", NULL);
	if (!ASSERT_OK(libbpf_get_error(skel->links.fentry_multi), "bpf_program__attach_tracing_multi"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.prog3);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");

	ASSERT_EQ(skel->bss->test_result, 8, "test_result");
	ASSERT_EQ(skel->bss->test3_result, 1, "test3_result");
	ASSERT_EQ(skel->bss->test4_result, 1, "test4_result");
	ASSERT_EQ(skel->bss->test5_result, 1, "test5_result");

cleanup:
	tracing_multi_single_test__destroy(skel);
}

/* multi link first, then single links  */
static void multi_single_2_test(void)
{
	LIBBPF_OPTS(bpf_tracing_multi_opts, mopts);
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct tracing_multi_single_test *skel = NULL;
	int err, prog_fd;

	skel = tracing_multi_single_test__open_and_load();
	if (!ASSERT_OK_PTR(skel, "fentry_multi_skel_load"))
		goto cleanup;

	skel->links.fentry_multi = bpf_program__attach_tracing_multi(skel->progs.fentry_multi, "bpf_fentry_test*", NULL);
	if (!ASSERT_OK(libbpf_get_error(skel->links.fentry_multi), "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.prog3 = bpf_program__attach_trace(skel->progs.prog3);
	if (!ASSERT_OK_PTR(link, "attach_fentry"))
		goto cleanup;

	skel->links.prog4 = bpf_program__attach_trace(skel->progs.prog4);
	if (!ASSERT_OK_PTR(link, "attach_fentry"))
		goto cleanup;

	skel->links.prog5 = bpf_program__attach_trace(skel->progs.prog5);
	if (!ASSERT_OK_PTR(link, "attach_fentry"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.prog3);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");

	ASSERT_EQ(skel->bss->test_result, 8, "test_result");
	ASSERT_EQ(skel->bss->test3_result, 1, "test3_result");
	ASSERT_EQ(skel->bss->test4_result, 1, "test4_result");
	ASSERT_EQ(skel->bss->test5_result, 1, "test5_result");

cleanup:
	tracing_multi_single_test__destroy(skel);
}

/* single, multi, single, multi, single */
static void multi_single_3_test(void)
{
	LIBBPF_OPTS(bpf_tracing_multi_opts, mopts);
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct tracing_multi_single_test *skel = NULL;
	int err, prog_fd;

	skel = tracing_multi_single_test__open_and_load();
	if (!ASSERT_OK_PTR(skel, "fentry_multi_skel_load"))
		goto cleanup;

	skel->links.prog3 = bpf_program__attach_trace(skel->progs.prog3);
	if (!ASSERT_OK_PTR(link, "attach_fentry"))
		goto cleanup;

	skel->links.fentry_multi = bpf_program__attach_tracing_multi(skel->progs.fentry_multi, "bpf_fentry_test*", NULL);
	if (!ASSERT_OK(libbpf_get_error(skel->links.fentry_multi), "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.prog4 = bpf_program__attach_trace(skel->progs.prog4);
	if (!ASSERT_OK_PTR(link, "attach_fentry"))
		goto cleanup;

	skel->links.fexit_multi = bpf_program__attach_tracing_multi(skel->progs.fexit_multi, "bpf_fentry_test*", NULL);
	if (!ASSERT_OK(libbpf_get_error(skel->links.fexit_multi), "bpf_program__attach_tracing_multi"))
                goto cleanup;

	skel->links.prog5 = bpf_program__attach_trace(skel->progs.prog5);
	if (!ASSERT_OK_PTR(link, "attach_fentry"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.prog3);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");

	/* we attach both multi fentry and fexit, so checking for 16  */
	ASSERT_EQ(skel->bss->test_result, 16, "test_result");
	ASSERT_EQ(skel->bss->test_ret_result, 8, "test_ret_result");
	ASSERT_EQ(skel->bss->test3_result, 1, "test3_result");
	ASSERT_EQ(skel->bss->test4_result, 1, "test4_result");
	ASSERT_EQ(skel->bss->test5_result, 1, "test5_result");

cleanup:
	tracing_multi_single_test__destroy(skel);
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
	if (test__start_subtest("single_1"))
		multi_single_1_test();
	if (test__start_subtest("single_2"))
		multi_single_2_test();
	if (test__start_subtest("single_3"))
		multi_single_3_test();
}
