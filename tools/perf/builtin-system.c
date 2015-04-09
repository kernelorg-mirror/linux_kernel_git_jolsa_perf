#include <stdio.h>
#include "perf.h"
#include "parse-options.h"
#include "builtin.h"
#include "topology.h"

static bool display_topo;

static const struct option system_options[] = {
	OPT_BOOLEAN(0, "topo", &display_topo, "display CPU topology"),
	OPT_END()
};

static const char * const system_usage[] = {
	"perf system [<options>]",
	NULL
};

static void display_topology(void)
{
	struct perf_tp tp;

	perf_tp__init(&tp);
	perf_tp__fprintf(stdout, &tp);
	perf_tp__clean(&tp);
}

int cmd_system(int argc, const char **argv, const char *prefix __maybe_unused)
{
	argc = parse_options(argc, argv, system_options, system_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc < 0)
		usage_with_options(system_usage, system_options);

	if (1 || display_topo)
		display_topology();

	return 0;
}
