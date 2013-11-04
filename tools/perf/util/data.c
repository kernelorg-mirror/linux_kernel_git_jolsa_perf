#include <linux/compiler.h>
#include <linux/kernel.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

#include "data.h"
#include "util.h"

#define MMAP_WRITE_SIZE   (64*1024*1024)

static bool check_pipe(struct perf_data_file *file)
{
	struct stat st;
	bool is_pipe = false;
	int fd = perf_data_file__is_read(file) ?
		 STDIN_FILENO : STDOUT_FILENO;

	if (!file->path) {
		if (!fstat(fd, &st) && S_ISFIFO(st.st_mode))
			is_pipe = true;
	} else {
		if (!strcmp(file->path, "-"))
			is_pipe = true;
	}

	if (is_pipe)
		file->fd = fd;

	return file->is_pipe = is_pipe;
}

static int check_backup(struct perf_data_file *file)
{
	struct stat st;

	if (!stat(file->path, &st) && st.st_size) {
		/* TODO check errors properly */
		char oldname[PATH_MAX];
		snprintf(oldname, sizeof(oldname), "%s.old",
			 file->path);
		unlink(oldname);
		rename(file->path, oldname);
	}

	return 0;
}

static int open_file_read(struct perf_data_file *file)
{
	struct stat st;
	int fd;

	fd = open(file->path, O_RDONLY);
	if (fd < 0) {
		int err = errno;

		pr_err("failed to open %s: %s", file->path, strerror(err));
		if (err == ENOENT && !strcmp(file->path, "perf.data"))
			pr_err("  (try 'perf record' first)");
		pr_err("\n");
		return -err;
	}

	if (fstat(fd, &st) < 0)
		goto out_close;

	if (!file->force && st.st_uid && (st.st_uid != geteuid())) {
		pr_err("file %s not owned by current user or root\n",
		       file->path);
		goto out_close;
	}

	if (!st.st_size) {
		pr_info("zero-sized file (%s), nothing to do!\n",
			file->path);
		goto out_close;
	}

	file->size = st.st_size;
	return fd;

 out_close:
	close(fd);
	return -1;
}

static int open_file_write(struct perf_data_file *file)
{
	if (check_backup(file))
		return -1;

	return open(file->path, O_CREAT|O_RDWR|O_TRUNC, S_IRUSR|S_IWUSR);
}

static int open_file(struct perf_data_file *file)
{
	int fd;

	fd = perf_data_file__is_read(file) ?
	     open_file_read(file) : open_file_write(file);

	file->fd = fd;
	return fd < 0 ? -1 : 0;
}

int perf_data_file__open(struct perf_data_file *file)
{
	if (check_pipe(file))
		return 0;

	if (!file->path)
		file->path = "perf.data";

	if (!file->mmap_size)
		file->mmap_size = MMAP_WRITE_SIZE;

	return open_file(file);
}

void perf_data_file__close(struct perf_data_file *file)
{
	close(file->fd);
}

static int do_mmap(struct perf_data_file *file, u64 offset)
{
	u64 mmap_size = file->mmap_size;

	file->mmap_off  = offset % mmap_size;
	file->mmap_foff = (offset / mmap_size) * mmap_size;

	file->mmap_addr = mmap(NULL, mmap_size,
			       PROT_WRITE | PROT_READ,
			       MAP_SHARED,
			       file->fd,
			       file->mmap_foff);

	if (file->mmap_addr == MAP_FAILED) {
		pr_err("mmap failed: %d: %s\n", errno, strerror(errno));
		return -1;
	}

	/* Expand file to include this mmap segment. */
	if (ftruncate(file->fd, file->mmap_foff + file->mmap_size) != 0) {
		pr_err("ftruncate failed: %d: %s\n", errno, strerror(errno));
		return -1;
	}

	return 0;
}

static ssize_t write_mmap(struct perf_data_file *file,
			  void *buf, size_t size)
{
	ssize_t total = size;

	if (!file->mmap_addr) {
		off_t offset = lseek(file->fd, 0, SEEK_CUR);
		if (offset < 0)
			return -1;

		if (do_mmap(file, offset))
			return -1;
	}

	while (size) {
		u64 remain = file->mmap_size - file->mmap_off;

		if (size > remain) {
			memcpy(file->mmap_addr + file->mmap_off, buf, remain);
			size -= remain;
			buf  += remain;

			munmap(file->mmap_addr, file->mmap_size);
			if (do_mmap(file, file->mmap_foff + file->mmap_size))
				return -1;
		} else {
			memcpy(file->mmap_addr + file->mmap_off, buf, size);
			file->mmap_off += size;
			size = 0;
		}
	}

	return total;
}

static ssize_t write_raw(struct perf_data_file *file,
			 void *buf, size_t size)
{
	ssize_t total = size;

	while (size) {
		ssize_t ret = write(file->fd, buf, size);

		if (ret < 0) {
			pr_err("failed to write perf data, error: %m\n");
			return -1;
		}

		size -= ret;
		buf  += ret;
	}

	return total;
}

ssize_t perf_data_file__write(struct perf_data_file *file,
			      void *buf, size_t size)
{
	return file->is_pipe ? write_raw(file, buf, size) :
			       write_mmap(file, buf, size);
}

int perf_data_file__munmap(struct perf_data_file *file)
{
	if (file->mmap_addr) {
		int ret;

		munmap(file->mmap_addr, file->mmap_size);

		file->mmap_addr = NULL;
		file->size = file->mmap_foff + file->mmap_off;

		ret = ftruncate(file->fd, file->size);
		if (ret)
			pr_err("ftruncate failed: %d: %s\n", errno,
			       strerror(errno));

		return ret;
	}

	return 0;
}
