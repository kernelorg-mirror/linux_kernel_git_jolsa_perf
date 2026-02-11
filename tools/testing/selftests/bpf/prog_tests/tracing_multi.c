// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include <bpf/btf.h>
#include <search.h>
#include "bpf/libbpf_internal.h"
#include "tracing_multi.skel.h"
#include "trace_helpers.h"

static int compare(const void *pa, const void *pb)
{
	return strcmp((char *) pa, (char *) pb);
}

static __u32 *get_ids(const char *funcs[], int funcs_cnt)
{
	size_t cap = 0, cnt = 0;
	__u32 nr, type_id;
	void *root = NULL;
	__u32 *ids = NULL;
	struct btf *btf;
	int i, err = -1;

	btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(btf, "btf__load_vmlinux_btf"))
		return NULL;

	for (i = 0; i < funcs_cnt; i++)
		tsearch(funcs[i], &root, compare);

	nr = btf__type_cnt(btf);
	for (type_id = 1; type_id < nr; type_id++) {
		const struct btf_type *type;
		const char *str;

		type = btf__type_by_id(btf, type_id);
		if (!type) {
			err = -1;
			break;
		}

		if (BTF_INFO_KIND(type->info) != BTF_KIND_FUNC)
			continue;

		str = btf__name_by_offset(btf, type->name_off);
		if (!str) {
			err = -1;
			break;
		}

		if (!tfind(str, &root, compare))
			continue;

		err = libbpf_ensure_mem((void **) &ids, &cap, sizeof(*ids), cnt + 1);
		if (err)
			break;

		ids[cnt++] = type_id;
	}

	if (err)
		free(ids);
	btf__free(btf);
	return ids;
}

static void tracing_multi_test_run(struct tracing_multi *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, prog_fd;

	prog_fd = bpf_program__fd(skel->progs.test_fentry);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");

	ASSERT_EQ(skel->bss->test_result_fentry, 8, "test_result_fentry");
	ASSERT_EQ(skel->bss->test_result_fexit, 8, "test_result_fexit");
}

static void test_skel_api(void)
{
	struct tracing_multi *skel = NULL;
	int err;

	skel = tracing_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi__open_and_load"))
		return;

	err = tracing_multi__attach(skel);
	if (!ASSERT_OK(err, "tracing_multi__attach"))
		goto cleanup;

	tracing_multi_test_run(skel);

cleanup:
	tracing_multi__destroy(skel);
}

static void test_link_api_pattern(void)
{
	struct tracing_multi *skel = NULL;

	skel = tracing_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi__open_and_load"))
		return;

	skel->links.test_fentry = bpf_program__attach_tracing_multi(skel->progs.test_fentry,
					"bpf_fentry_test*", NULL);
	if (!ASSERT_OK_PTR(skel->links.test_fentry, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.test_fexit = bpf_program__attach_tracing_multi(skel->progs.test_fexit,
					"bpf_fentry_test*", NULL);
	if (!ASSERT_OK_PTR(skel->links.test_fexit, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	tracing_multi_test_run(skel);

cleanup:
	tracing_multi__destroy(skel);
}

static void test_link_api_ids(void)
{
	LIBBPF_OPTS(bpf_tracing_multi_opts, opts);
	const char *funcs[] = {
		"bpf_fentry_test1",
		"bpf_fentry_test2",
		"bpf_fentry_test3",
		"bpf_fentry_test4",
		"bpf_fentry_test5",
		"bpf_fentry_test6",
		"bpf_fentry_test7",
		"bpf_fentry_test8",
	};
	size_t cnt = ARRAY_SIZE(funcs);
	struct tracing_multi *skel = NULL;
	__u32 *ids;

	skel = tracing_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi__open_and_load"))
		return;

	ids = get_ids(funcs, cnt);
	if (!ASSERT_OK_PTR(ids, "get_ids"))
		goto cleanup;

	opts.ids = ids;
	opts.cnt = cnt;

	skel->links.test_fentry = bpf_program__attach_tracing_multi(skel->progs.test_fentry,
						NULL, &opts);
	if (!ASSERT_OK_PTR(skel->links.test_fentry, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.test_fexit = bpf_program__attach_tracing_multi(skel->progs.test_fexit,
						NULL, &opts);
	if (!ASSERT_OK_PTR(skel->links.test_fexit, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	tracing_multi_test_run(skel);

cleanup:
	tracing_multi__destroy(skel);
}

static void __test_intersect(void)
{
}

static void test_intersect_fentry(void)
{
	struct tracing_multi_fentry_test *skel = NULL;
	LIBBPF_OPTS(bpf_tracing_multi_opts, opts);
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	const char *funcs_1[] = {
		"bpf_fentry_test1",
		"bpf_fentry_test2",
		"bpf_fentry_test3",
		"bpf_fentry_test4",
		"bpf_fentry_test5",
	};
	const char *funcs_2[] = {
		"bpf_fentry_test4",
		"bpf_fentry_test5",
		"bpf_fentry_test6",
		"bpf_fentry_test7",
		"bpf_fentry_test8",
	};
	__u32 *ids_1 = NULL, *ids_2 = NULL;
	size_t cnt_1 = ARRAY_SIZE(funcs_1);
	size_t cnt_2 = ARRAY_SIZE(funcs_2);
	struct bpf_link *link_1 = NULL;
	struct bpf_link *link_2 = NULL;
	int err, prog_fd;

	skel = tracing_multi_fentry_test__open_and_load();
	if (!ASSERT_OK_PTR(skel, "fentry_multi_skel_load"))
		goto cleanup;

	__test_intersect(skel->progs.test_fentry, &skel->bss->test_result, funcs, cnt,
			 skel->progs.test_fentry_2, &skel->bss->test_result_2, funcs_2, cnt_2);
			

	ids_1 = get_ids(funcs_1, cnt_1);
	if (!ASSERT_OK_PTR(ids_1, "get_ids"))
		goto cleanup;
	ids_2 = get_ids(funcs_2, cnt_2);
	if (!ASSERT_OK_PTR(ids_2, "get_ids"))
		goto cleanup;

	opts.btf_ids = ids_1;
	opts.cnt = cnt_1;

	link_1 = bpf_program__attach_tracing_multi(skel->progs.test_1, NULL, &opts);
	if (!ASSERT_OK_PTR(link_1, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	opts.btf_ids = ids_2;
	opts.cnt = cnt_2;

	link_2 = bpf_program__attach_tracing_multi(skel->progs.test_2, NULL, &opts);
	if (!ASSERT_OK_PTR(link_2, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.test);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");

	ASSERT_EQ(skel->bss->test_result_2, 5, "test_result");
	ASSERT_EQ(skel->bss->test_result_3, 5, "test_result");

cleanup:
	free(ids_1);
	free(ids_2);
	bpf_link__destroy(link_1);
	bpf_link__destroy(link_2);
	tracing_multi_fentry_test__destroy(skel);
}

void test_tracing_multi_test(void)
{
#ifndef __x86_64__
	test__skip();
#endif

	if (test__start_subtest("skel_api"))
		test_skel_api();
	if (test__start_subtest("link_api_pattern"))
		test_link_api_pattern();
	if (test__start_subtest("link_api_ids"))
		test_link_api_ids();
	if (test__start_subtest("intersect/fentry"))
		test_intersect_fentry();
}
