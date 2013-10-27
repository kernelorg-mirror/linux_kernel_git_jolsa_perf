
#include "data.h"

int perf_data__open(struct perf_data *data)
{
	struct perf_data_file *file = perf_data__file(data);

	file->path  = data->path;
	file->mode  = data->mode;
	file->force = data->force;

	return perf_data_file__open(&data->file);
}

void perf_data__close(struct perf_data *data)
{
	perf_data_file__close(&data->file);
}
