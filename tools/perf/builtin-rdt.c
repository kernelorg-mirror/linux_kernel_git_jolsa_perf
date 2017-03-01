#include <stdio.h>
#include <linux/compiler.h>
#include "perf.h"
#include "builtin.h"
#include "debug.h"
#include "workload.h"
#include <subcmd/parse-options.h>
#include <api/fs/fs.h>
#include "rdt.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int move_task(int pid, const char *group)
{
	char path[PATH_MAX], buf[20];
	struct stat st;
	FILE *file;
	int cnt;

	scnprintf(path, PATH_MAX, "%s/%s/tasks", resctrlfs__mount(), group);

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

	if (1 != fwrite(buf, cnt, 1, file))
		pr_err("failed: to write to '%s'\n", path);

	fclose(file);
	return 0;
}

static int setup_resctrl(void)
{
	if (!resctrlfs__mount()) {
		pr_err("failed: no resctrl fs mount found\n");
		return -1;
	}

	return 0;
}

static int perf_rdt__dump(int argc, const char **argv)
{
	bool close_file = false;
	FILE *file_out = stdout;
	int ret;

	setup_pager();

	if (argc == 2) {
		file_out = fopen(argv[1], "w+");
		if (!file_out)
			return -1;
		close_file = true;
	}

	ret = rdt_dump(file_out);

	if (close_file)
		fclose(file_out);
	return ret;
}

int cmd_rdt(int argc, const char **argv)
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

		if (!perf_workload__start(&workload))
			return -1;

		wait(&status);
	} else if (!strncmp(argv[0], "dump", 4)) {
		return perf_rdt__dump(argc, argv);
	} else {
                usage_with_options(rdt_usage, rdt_options);
	}

	return 0;
}
