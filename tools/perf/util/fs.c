
/* TODO merge/factor into tools/lib/lk/debugfs.c */

#include "util.h"
#include "util/fs.h"

static const char * const sysfs_known_mountpoints[] = {
	"/sys",
	0,
};

struct perf_fs {
	const char		*name;
	const char * const	*mounts;
	char			 path[PATH_MAX + 1];
	bool			 found;
	long			 magic;
};

enum {
	FS_SYSFS = 0,
};

static struct perf_fs fss[] = {
	[FS_SYSFS] = {
		.name	= "sysfs",
		.mounts	= sysfs_known_mountpoints,
		.magic	= SYSFS_MAGIC,
	},
};

static bool read_mounts(struct perf_fs *fs)
{
	bool found = false;
	char type[100];
	FILE *fp;

	fp = fopen("/proc/mounts", "r");
	if (fp == NULL)
		return NULL;

	while (!found &&
	       fscanf(fp, "%*s %" STR(PATH_MAX) "s %99s %*s %*d %*d\n",
		      fs->path, type) == 2) {

		if (strcmp(type, fs->name) == 0)
			found = true;
	}

	fclose(fp);
	return fs->found = found;
}

static int valid_mount(const char *fs, long magic)
{
	struct statfs st_fs;

	if (statfs(fs, &st_fs) < 0)
		return -ENOENT;
	else if (st_fs.f_type != magic)
		return -ENOENT;

	return 0;
}

static bool check_mounts(struct perf_fs *fs)
{
	const char * const *ptr;

	ptr = fs->mounts;
	while (*ptr) {
		if (valid_mount(*ptr, fs->magic) == 0) {
			fs->found = true;
			strcpy(fs->path, *ptr);
			return true;
		}
		ptr++;
	}

	return false;
}

static const char *get_mountpoint(struct perf_fs *fs)
{
	if (check_mounts(fs))
		return fs->path;

	return read_mounts(fs) ? fs->path : NULL;
}

static const char *find_mountpoint(int idx)
{
	struct perf_fs *fs = &fss[idx];

	if (fs->found)
		return (const char *) fs->path;

	return get_mountpoint(fs);
}

#define FIND_MOUNTPOINT(name, idx)		\
const char *name##_find_mountpoint(void)	\
{						\
	return find_mountpoint(idx);		\
}

FIND_MOUNTPOINT(sysfs, FS_SYSFS);
