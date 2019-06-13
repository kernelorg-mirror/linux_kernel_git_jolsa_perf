// SPDX-License-Identifier: GPL-2.0
#include <dirent.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "string2.h"
#include <api/strlist.h>
#include <string.h>
#include "asm/bug.h"
#include "thread_map.h"
#include "util.h"
#include "debug.h"
#include "event.h"

size_t thread_map__fprintf(struct perf_thread_map *threads, FILE *fp)
{
	int i;
	size_t printed = fprintf(fp, "%d thread%s: ",
				 perf_thread_map__nr(threads),
				 perf_thread_map__nr(threads) > 1 ? "s" : "");
	for (i = 0; i < perf_thread_map__nr(threads); ++i)
		printed += fprintf(fp, "%s%d", i ? ", " : "", perf_thread_map__pid(threads, i));

	return printed + fprintf(fp, "\n");
}

static void thread_map__copy_event(struct perf_thread_map *threads,
				   struct thread_map_event *event)
{
	unsigned i;

	for (i = 0; i < event->nr; i++) {
		perf_thread_map__set_pid(threads, i, (pid_t) event->entries[i].pid);
		perf_thread_map__set_comm(threads, i, event->entries[i].comm);
	}
}

struct perf_thread_map *thread_map__new_event(struct thread_map_event *event)
{
	struct perf_thread_map *threads;

	threads = perf_thread_map__empty_new(event->nr);
	if (threads)
		thread_map__copy_event(threads, event);

	return threads;
}
