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
#include <errno.h>

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

static int dump_display(FILE *file, char *path)
{
	struct rdt_data data;

	if (rdt_parse(&data, path))
		return -1;

	return rdt_display(file, &data, false);
}

static int perf_rdt__dump(int argc, const char **argv)
{
	const char * const dump_usage[] = {
		"perf rdt dump [<options>] [file]",
		NULL
	};
	bool json = false;
	const struct option dump_options[] = {
	OPT_BOOLEAN('j', "json", &json, "Dump json data."),
	OPT_END()
	};
	char tmp_path[PATH_MAX];
	bool close_file = false;
	FILE *file_dump;
	FILE *file_out = stdout;
	int ret, fd_dump;

	argc = parse_options(argc, argv, dump_options, dump_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc == 1) {
		file_out = fopen(argv[1], "w+");
		if (!file_out)
			return -1;
		close_file = true;
	}

	setup_pager();

	if (json) {
		file_dump = file_out;
	} else {
		scnprintf(tmp_path, PATH_MAX, "/tmp/perf-rdt-dump-XXXXXX");

		fd_dump = mkstemp(tmp_path);
		if (fd_dump < 0)
			return -1;

		file_dump = fdopen(fd_dump, "r+");
		if (!file_dump)
			return -1;
	}

	ret = rdt_dump(file_dump);
	if (ret)
		goto out;

	if (!json) {
		fclose(file_dump);
		ret = dump_display(file_out, tmp_path);
	}

out:
	if (close_file)
		fclose(file_out);
	return ret;
}

#define STRDUP_FAIL_EXIT(s)		\
	({	char *_p = strdup(s);	\
		if (!_p)		\
			return -ENOMEM;	\
		_p;			\
	})

static int perf_rdt__stat(int argc, const char **argv)
{
	const char * const stat_args[] = {
		"stat",
		"-e intel_cqm/llc_occupancy/,intel_cqm/local_bytes/,intel_cqm/total_bytes/",
	};
	const char **stat_argv;
	unsigned int i, j, stat_argc;

	stat_argc = ARRAY_SIZE(stat_args) + argc;

        stat_argv = calloc(stat_argc + 1, sizeof(char *));
	if (!stat_argv)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(stat_args); i++)
		stat_argv[i] = STRDUP_FAIL_EXIT(stat_args[i]);

	for (j = 1; j < (unsigned int)argc; j++, i++)
		stat_argv[i] = argv[j];

	return cmd_stat(i, stat_argv);
}

static int perf_rdt__record(int argc, const char **argv)
{
	const char * const record_args[] = {
		"record",
		"--closid",
	};
	const char **record_argv;
	unsigned int i, j, record_argc;

	record_argc = ARRAY_SIZE(record_args) + argc;

        record_argv = calloc(record_argc + 1, sizeof(char *));
	if (!record_argv)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(record_args); i++)
		record_argv[i] = STRDUP_FAIL_EXIT(record_args[i]);

	for (j = 1; j < (unsigned int)argc; j++, i++)
		record_argv[i] = argv[j];

	return cmd_record(i, record_argv);
}

static int perf_rdt__report(int argc, const char **argv)
{
	const char * const report_args[] = {
		"report",
		"-s rdt_group",
	};
	const char **report_argv;
	unsigned int i, j, report_argc;

	report_argc = ARRAY_SIZE(report_args) + argc;

        report_argv = calloc(report_argc + 1, sizeof(char *));
	if (!report_argv)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(report_args); i++)
		report_argv[i] = STRDUP_FAIL_EXIT(report_args[i]);

	for (j = 1; j < (unsigned int)argc; j++, i++)
		report_argv[i] = argv[j];

	return cmd_report(i, report_argv);
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
	} else if (!strncmp(argv[0], "stat", 4)) {
		return perf_rdt__stat(argc, argv);
	} else if (!strncmp(argv[0], "record", 4)) {
		return perf_rdt__record(argc, argv);
	} else if (!strncmp(argv[0], "report", 4)) {
		return perf_rdt__report(argc, argv);
	} else {
                usage_with_options(rdt_usage, rdt_options);
	}

	return 0;
}
