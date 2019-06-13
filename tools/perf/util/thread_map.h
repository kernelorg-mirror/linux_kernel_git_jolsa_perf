/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_THREAD_MAP_H
#define __PERF_THREAD_MAP_H

#include <sys/types.h>
#include <stdio.h>
#include <perf/threadmap.h>

struct thread_map_event;

struct perf_thread_map *thread_map__new_event(struct thread_map_event *event);

size_t thread_map__fprintf(struct perf_thread_map *threads, FILE *fp);

#endif	/* __PERF_THREAD_MAP_H */
