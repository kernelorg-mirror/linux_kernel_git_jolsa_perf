#include <stdio.h>
#include <linux/compiler.h>
#include "perf.h"
#include "builtin.h"
#include "debug.h"
#include <subcmd/parse-options.h>
#include <api/fs/fs.h>
#include "rdt.h"

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

static int dump_resources(FILE *file __maybe_unused)
{
	char path[PATH_MAX];
	static const char *name[RDT_NUM_RESOURCES] = {
		"L3", "L3DATA", "L3CODE", "L2",
	};
	int i;

	for (i = 0; i < RDT_NUM_RESOURCES; i++) {
		scnprintf(path, PATH_MAX, "%s/info/%s", resctrlfs, name[i]);

	}

	return 0;
}

static int dump_groups(FILE *file __maybe_unused)
{
	return 0;
}

static int perf_rdt__dump(int argc, const char **argv)
{
	bool close_file = false;
	FILE *file = stdout;
	int ret;

	if (setup_resctrl())
		return -1;

	if (argc == 2) {
		file = fopen(argv[1], "w+");
		if (!file)
			return -1;
		close_file = true;
	}

	ret = dump_resources(file) ||
	      dump_groups(file);

	if (close_file)
		fclose(file);
	return ret;
}

int cmd_rdt(int argc, const char **argv)
{
	const char * const rdt_usage[] = {
		"perf rdt [<options>] <command>",
		"perf rdt [<options>] -- <command> [<options>]",
		NULL
	};
	const struct option rdt_options[] = {
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_END()
	};

	argc = parse_options(argc, argv, rdt_options, rdt_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
        if (!argc)
                usage_with_options(rdt_usage, rdt_options);

	if (!strncmp(argv[0], "dump", 4)) {
		return perf_rdt__dump(argc, argv);
	} else {
                usage_with_options(rdt_usage, rdt_options);
	}

	return 0;
}
