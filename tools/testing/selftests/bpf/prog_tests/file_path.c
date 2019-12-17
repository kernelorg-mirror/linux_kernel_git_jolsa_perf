// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/limits.h>

struct data {
	uint64_t	my_pid_tgid;
	char		path[4096];
};

static size_t roundup_page(size_t sz)
{
	long page_size = sysconf(_SC_PAGE_SIZE);
	return (sz + page_size - 1) / page_size * page_size;
}

void test_file_path(void)
{
	const size_t mmap_sz = roundup_page(sizeof(struct data));
	struct bpf_link *link_fentry = NULL;
	struct bpf_prog_load_attr attr = {
		.file = "file_path.o",
	};
	struct bpf_object *obj = NULL;
	struct bpf_program *fentry;
        struct bpf_map *data_map;
	int err, file_path_fd;
        uint64_t my_pid_tgid, buf;
	void *mmap_data = NULL;
	struct data *data;
	int fd, duration = 0;
	char path[PATH_MAX], cwd[PATH_MAX];

	my_pid_tgid = getpid() | ((uint64_t) syscall(SYS_gettid) << 32);

	err = bpf_prog_load_xattr(&attr, &obj, &file_path_fd);
	if (CHECK(err, "prog_load raw tp", "err %d errno %d\n", err, errno))
		goto cleanup;

	fentry = bpf_object__find_program_by_title(obj, "fentry/vfs_read");
	if (CHECK(!fentry, "find_prog", "prog vfs_read not found\n"))
		goto cleanup;

	data_map = bpf_object__find_map_by_name(obj, "file_pat.bss");
	if (CHECK(!data_map, "find_data_map", "data map not found\n"))
		goto cleanup;

	mmap_data = mmap(NULL, mmap_sz, PROT_READ | PROT_WRITE,
			 MAP_SHARED, bpf_map__fd(data_map), 0);
	if (CHECK(mmap_data == MAP_FAILED, "mmap",
		  ".bss mmap failed: %d", errno)) {
		mmap_data = NULL;
		goto cleanup;
	}

	data = mmap_data;

	memset(mmap_data, 0, sizeof(*data));
	data->my_pid_tgid = my_pid_tgid;

	link_fentry = bpf_program__attach_trace(fentry);
	if (CHECK(IS_ERR(link_fentry), "attach fentry", "err %ld\n",
		  PTR_ERR(link_fentry)))
		goto cleanup;

	fd = open(attr.file, O_RDONLY);
	if (CHECK(fd == -1, "open failed", "err %d\n", fd))
		goto cleanup;

	err = read(fd, &buf, sizeof(buf));
	if (CHECK(err == -1, "read failed", "err %d\n", err))
		goto cleanup;

	close(fd);

	getcwd(cwd, sizeof(cwd));
	CHECK_FAIL(snprintf(path, sizeof(path), "%s/%s", cwd, attr.file) < 0);
	CHECK_FAIL(strcmp(path, data->path));

cleanup:
	if (mmap_data) {
		CHECK_FAIL(munmap(mmap_data, mmap_sz));
		mmap_data = NULL;
	}
	if (!IS_ERR_OR_NULL(link_fentry))
		bpf_link__destroy(link_fentry);
	bpf_object__close(obj);
}
