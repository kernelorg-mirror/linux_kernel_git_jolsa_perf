#include "perf.h"
#include "builtin.h"
#include "debug.h"
#include "workload.h"
#include <linux/compiler.h>
#include <subcmd/parse-options.h>
#include <api/fs/fs.h>
#include <sys/types.h>
#include <sys/stat.h>

static const char *resctrlfs;

static int setup_resctrl(void)
{
	resctrlfs = resctrlfs__mount();
	if (!resctrlfs) {
		pr_err("failed: no resctrl fs mount found\n");
		return -1;
	}

	return 0;
}

static int move_task(int pid, const char *group)
{
	char path[PATH_MAX], buf[20];
	struct stat st;
	FILE *file;
	int cnt;

	scnprintf(path, PATH_MAX, "%s/%s/tasks", resctrlfs, group);

	if (stat(path, &st)) {
		pr_err("failed: group not found\n");
		return -1;
	}

	file = fopen(path, "w");
	if (!file) {
		pr_err("failed: open file '%s'\n", path);
		return -1;
	}

	cnt = scnprintf(buf, 20, "%d\n", pid);
	if (cnt != (int) fwrite(buf, cnt, 1, file))
		pr_err("failed: to write to '%s'\n", path);

	fclose(file);
	return 0;
}

int cmd_rdt(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const char * const rdt_usage[] = {
		"perf rdt [<options>] <command>",
		"perf rdt [<options>] -- <command> [<options>]",
		NULL
	};
	static const char *group;
	const struct option rdt_options[] = {
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_STRING('g', "group", &group, "group",
		   "group to attach workload to"),
	OPT_END()
	};

	if (setup_resctrl())
		return -1;

	argc = parse_options(argc, argv, rdt_options, rdt_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc)
		usage_with_options(rdt_usage, rdt_options);

	if (group) {
		struct perf_workload workload;
		int status;

		if (perf_workload__prepare(&workload, argv, false, NULL))
			return -1;

		if (move_task(workload.pid, group))
			return -1;

		if (perf_workload__start(&workload))
			return -1;

		wait(&status);
	}

	return 0;
}
