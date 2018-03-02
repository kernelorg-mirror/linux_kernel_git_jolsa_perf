#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <linux/compiler.h>
#include <subcmd/parse-options.h>
#include "builtin.h"
#include "perf.h"
#include "target.h"
#include "debug.h"
#include "parse-events.h"
#include "evlist.h"
#include "evsel.h"
#include "bpf-loader.h"

struct perf_bpf {
	struct target		 target;
	struct perf_evlist	*evlist;
};

struct perf_bpf bpf = {
	.target = { .uid = UINT_MAX, },
};

static volatile int done;
static volatile int workload_exec_errno;

static void sig_handler(int sig __maybe_unused)
{
	done = 1;
}

/*
 * perf_evlist__prepare_workload will send a SIGUSR1
 * if the fork fails, since we asked by setting its
 * want_signal to true.
 */
static void workload_exec_failed_signal(int signo __maybe_unused, siginfo_t *info,
					void *ucontext __maybe_unused)
{
	workload_exec_errno = info->si_value.sival_int;
}

static int create_perf_bpf_counter(struct perf_evsel *evsel)
{
	if (target__has_cpu(&bpf.target) && !target__has_per_thread(&bpf.target))
		return perf_evsel__open_per_cpu(evsel, perf_evsel__cpus(evsel));

	return perf_evsel__open_per_thread(evsel, bpf.evlist->threads);
}

static int __cmd_bpf(int argc , const char **argv)
{
	struct perf_evsel *evsel;
	bool forks = argc > 0;
	int err, status;
	int child_pid = -1;
	char msg[BUFSIZ];
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 500 };

	if (forks) {
		err = perf_evlist__prepare_workload(bpf.evlist, &bpf.target,
						    argv, true,
						    workload_exec_failed_signal);
		if (err < 0) {
			pr_err("Couldn't run the workload!\n");
			status = err;
			goto out;
		}

		child_pid = bpf.evlist->workload.pid;
	}

        evlist__for_each_entry(bpf.evlist, evsel) {
                err =create_perf_bpf_counter(evsel);
		if (err < 0) {
                        perf_evsel__open_strerror(evsel, &bpf.target,
                                                  errno, msg, sizeof(msg));
                        pr_err("%s\n", msg);
			goto out_child;
                }
	}

	err = bpf__apply_obj_config();
	if (err) {
		char errbuf[BUFSIZ];

		bpf__strerror_apply_obj_config(err, errbuf, sizeof(errbuf));
					       pr_err("ERROR: Apply config to BPF failed: %s\n",
					       errbuf);
		goto out_child;
	}

	if (forks)
		perf_evlist__start_workload(bpf.evlist);

	if (!target__none(&bpf.target))
		perf_evlist__enable(bpf.evlist);

	while (!done) {
		if (forks) {
			int pid = waitpid(child_pid, &status, WNOHANG);

			if (pid > 0 || pid < 0)
				break;
		}

		nanosleep(&ts, NULL);
	}

	if (!target__none(&bpf.target))
		perf_evlist__disable(bpf.evlist);

	child_pid = -1;

out_child:
	if (forks) {
                if (workload_exec_errno) {
                        const char *emsg = str_error_r(workload_exec_errno, msg, sizeof(msg));
                        pr_err("Workload failed: %s\n", emsg);
                        return -1;
                }

		if (child_pid != -1) {
			kill(child_pid, SIGTERM);
			waitpid(child_pid, &status, 0);
		}

                if (WIFSIGNALED(status))
                        psignal(WTERMSIG(status), argv[0]);
	}

out:
	perf_evlist__close(bpf.evlist);

	if (err)
		status = err;
	return WEXITSTATUS(status);
}

int cmd_bpf(int argc, const char **argv)
{
	int err = -1;
	const char * const bpf_usage[] = {
		"perf bpf [<options>] [<command>]",
		"perf bpf [<options>] -- <command> [<options>]",
		NULL
	};
	const struct option bpf_options[] = {
		OPT_CALLBACK('e', "event", &bpf.evlist, "event",
			     "event selector. use 'perf list' to list available events",
			     parse_events_option),
		OPT_STRING('C', "cpu", &bpf.target.cpu_list, "cpu",
			   "list of cpus to monitor"),
		OPT_BOOLEAN('a', "all-cpus", &bpf.target.system_wide,
			    "system-wide collection from all CPUs"),
		OPT_STRING('p', "pid", &bpf.target.pid, "pid",
			   "record events on existing process id"),
		OPT_STRING('t', "tid", &bpf.target.tid, "tid",
			   "record events on existing thread id"),
		OPT_INCR('v', "verbose", &verbose,
			 "be more verbose"),
		OPT_END()
	};

	signal(SIGINT, sig_handler);

	bpf.evlist = perf_evlist__new();
	if (bpf.evlist == NULL)
		return -ENOMEM;

	argc = parse_options(argc, argv, bpf_options, bpf_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc && target__none(&bpf.target))
		usage_with_options(bpf_usage, bpf_options);

	if (bpf.evlist->nr_entries == 0) {
		pr_err("failed: No event specified\n");
		goto out;
	}

	if (perf_evlist__create_maps(bpf.evlist, &bpf.target) < 0) {
		if (target__has_task(&bpf.target)) {
			pr_err("Problems finding threads of monitor\n");
			parse_options_usage(bpf_usage, bpf_options, "p", 1);
			parse_options_usage(NULL, bpf_options, "t", 1);
		} else if (target__has_cpu(&bpf.target)) {
			perror("failed to parse CPUs map");
			parse_options_usage(bpf_usage, bpf_options, "C", 1);
			parse_options_usage(NULL, bpf_options, "a", 1);
		}
	}

	target__validate(&bpf.target);

	err = __cmd_bpf(argc, argv);
out:
	perf_evlist__delete(bpf.evlist);
	return err;
}
