// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <sys/wait.h>
#include <test_progs.h>
#include <unistd.h>

#include "tp_deny_list_success.skel.h"
#include "tp_deny_list_fail.skel.h"

static size_t log_buf_sz = 1 << 20; /* 1 MB */
static char obj_log_buf[1048576];

static void run_success_load_test(void)
{
	struct tp_deny_list_success *skel;
	int err;

	skel = tp_deny_list_success__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	err = tp_deny_list_success__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

cleanup:
	tp_deny_list_success__destroy(skel);
}

static struct {
	const char *prog_name;
	const char *expected_err_msg;
} failure_tests[] = {
	{"tp_btf_contention_begin_printk",
	 "Can't attach program with bpf_trace_printk#6 helper to contention_begin tracepoint."},
	{"tp_btf_bpf_trace_printk_printk",
	 "Can't attach program with bpf_trace_vprintk#177 helper to bpf_trace_printk tracepoint."}
};

static void verify_fail(const char *prog_name, const char *expected_err_msg)
{
	LIBBPF_OPTS(bpf_object_open_opts, opts);
	struct tp_deny_list_fail *skel;
	int err, i;

	opts.kernel_log_buf = obj_log_buf;
	opts.kernel_log_size = log_buf_sz;
	opts.kernel_log_level = 1;

	skel = tp_deny_list_fail__open_opts(&opts);
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		goto cleanup;

	for (i = 0; i < ARRAY_SIZE(failure_tests); i++) {
		struct bpf_program *prog;
		const char *curr_name = failure_tests[i].prog_name;

		prog = bpf_object__find_program_by_name(skel->obj, curr_name);
		if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
			goto cleanup;

		bpf_program__set_autoload(prog, !strcmp(curr_name, prog_name));
	}

	err = tp_deny_list_fail__load(skel);
	if (!ASSERT_ERR(err, "unexpected load success"))
		goto cleanup;

	if (!ASSERT_OK_PTR(strstr(obj_log_buf, expected_err_msg), "expected_err_msg")) {
		fprintf(stderr, "Expected err_msg: %s\n", expected_err_msg);
		fprintf(stderr, "Verifier output: %s\n", obj_log_buf);
	}

cleanup:
	tp_deny_list_fail__destroy(skel);
}

void test_tp_deny_list_load(void)
{
	int i;

	// test we can load all the tracepoints in tp_deny_list_success
	run_success_load_test();

	// test that load fails for tracepoints in tp_deny_list_fail
	for (i = 0; i < ARRAY_SIZE(failure_tests); i++)
		verify_fail(failure_tests[i].prog_name, failure_tests[i].expected_err_msg);
}

static const char * const success_attach[] = {
	"tp_btf_contention_begin_empty",
	"tp_btf_bpf_trace_printk_empty",
};

static const char * const fail_attach[] = {
	"raw_tp_contention_begin_empty",
	"raw_tp_bpf_trace_printk_empty",
	"raw_tp_contention_begin_printk",
	"raw_tp_bpf_trace_printk_printk",
};

static int attach_tp(struct tp_deny_list_success *skel, const char *prog_name)
{
	struct bpf_program *prog;
	struct bpf_link *link;
	int err = 0;

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
		return -123;

	link = bpf_program__attach(prog);
	err = libbpf_get_error(link);
	if (!err)
		bpf_link__destroy(link);
	return err;
}

void test_tp_deny_list_attach(void)
{
	struct tp_deny_list_success *skel;
	int i, err;

	skel = tp_deny_list_success__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	err = tp_deny_list_success__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	for (i = 0; i < ARRAY_SIZE(success_attach); i++) {
		if (!ASSERT_OK(attach_tp(skel, success_attach[i]), "attach_tp_success"))
			goto cleanup;
	}

	for (i = 0; i < ARRAY_SIZE(fail_attach); i++) {
		if (!ASSERT_EQ(attach_tp(skel, fail_attach[i]), -EACCES, "attach_tp_fail"))
			goto cleanup;
	}

cleanup:
	tp_deny_list_success__destroy(skel);
}
