#ifndef __PERF_DATA_H
#define __PERF_DATA_H

enum perf_data_mode {
	PERF_DATA_MODE_WRITE,
	PERF_DATA_MODE_READ,
};

#include "data_file.h"

struct perf_data {
	const char		*path;
	bool			 force;
	enum perf_data_mode	 mode;
	bool			 is_dir;
	struct perf_data_file	 file;
};

#define perf_data__file(data) (&(data)->file)

static inline int perf_data__is_pipe(struct perf_data *data)
{
	return perf_data_file__is_pipe(&data->file);
}

static inline bool perf_data__is_read(struct perf_data *data)
{
	return data->mode == PERF_DATA_MODE_READ;
}

static inline bool perf_data__is_write(struct perf_data *data)
{
	return data->mode == PERF_DATA_MODE_WRITE;
}

int perf_data__open(struct perf_data *data);
void perf_data__close(struct perf_data *data);

#endif /* __PERF_DATA_H */
