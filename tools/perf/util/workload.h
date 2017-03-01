#ifndef __PERF_WORKLOAD_H
#define __PERF_WORKLOAD_H

#include <signal.h>

struct perf_workload {
	int	cork_fd;
	pid_t	pid;
};

int perf_workload__prepare(struct perf_workload *workload,
			   const char *argv[], bool pipe_output,
			   void (*exec_error)(int signo, siginfo_t *info,
			   void *ucontext));
int perf_workload__start(struct perf_workload *workload);

#endif /* __PERF_WORKLOAD_H */
