// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include <linux/hw_breakpoint.h>
#include "bp_modify.skel.h"

void test_bp_modify(void)
{
	struct bp_modify *skel;
	int err, cpus, i;

	cpus = libbpf_num_possible_cpus();

	struct bpf_link *links[cpus] = {};

	skel = bp_modify__open_and_load();
	if (!ASSERT_OK_PTR(skel, "bp_modify__open_and_load"))
		return;

	err = bp_modify__attach(skel);
	if (!ASSERT_OK(err, "bp_modify__attach"))
		goto cleanup;

	for (i = 0; i < cpus; i++) {
		struct perf_event_attr attr;
		struct bpf_link *lnk;
		int fd;

		memset(&attr, 0, sizeof(attr));
		attr.size = sizeof(attr);
		attr.type = PERF_TYPE_BREAKPOINT;
		attr.bp_type = HW_BREAKPOINT_W;
		attr.bp_addr = 0;
		attr.bp_len = sizeof(long);
		attr.sample_period = 1;

		fd = syscall(__NR_perf_event_open, &attr, -1, i, -1, PERF_FLAG_FD_CLOEXEC);
		if (!ASSERT_GE(fd, 0, "perf_event_open"))
			goto cleanup;

		err = bpf_map_update_elem(bpf_map__fd(skel->maps.events), &i, &fd, 0);
		if (!ASSERT_OK(err, "bpf_map_update_elem"))
			goto cleanup;

		lnk = bpf_program__attach_perf_event_opts(skel->progs.overflow, fd, NULL);
		if (!ASSERT_OK_PTR(lnk, "bpf_program__attach_perf_event_opts"))
			goto cleanup;
		links[i] = lnk;
	}

	getchar();

cleanup:
	for (i = 0; i < cpus && links[i]; i++)
		bpf_link__destroy(links[i]);

	bp_modify__destroy(skel);
}
