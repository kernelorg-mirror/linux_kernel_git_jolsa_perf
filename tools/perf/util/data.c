
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#include "data.h"
#include "util.h"

static int backup_dir(struct perf_data *data __maybe_unused)
{
	return 0;
}

static bool is_dir_chk(struct perf_data *data)
{
	struct stat st;

	if (stat(data->path, &st))
		return false;

	return S_ISDIR(st.st_mode);
}

static int check_dir(struct perf_data *data)
{
	bool is_dir = is_dir_chk(data);
	int err = 0;

	switch (data->mode) {
	case PERF_DATA_MODE_READ:
		data->is_dir = is_dir;
		break;
	case PERF_DATA_MODE_WRITE:
		if (data->is_dir && is_dir)
			err = backup_dir(data);
		break;
	default:
		err = -1;
		break;
	}

	return err;
}

static int dir_header(struct perf_data *data,
		      struct perf_data_file *file)
{
	char *path = malloc(PATH_MAX);

	if (path) {
		scnprintf(path, PATH_MAX, "%s/header", data->path);
		file->path = path;
	}

	return path ? 0 : -ENOMEM;
}

int perf_data__open(struct perf_data *data)
{
	struct perf_data_file *header = perf_data__file(data);

	if (check_dir(data))
		return -1;

	if (data->is_dir) {
		if (dir_header(data, header))
			return -ENOMEM;
	} else
		header->path = strdup(data->path);

	header->mode  = data->mode;
	header->force = data->force;

	return perf_data_file__open(header);
}

void perf_data__close(struct perf_data *data)
{
	struct perf_data_file *header = perf_data__file(data);

	perf_data_file__close(header);
	free((void *) header->path);
}
