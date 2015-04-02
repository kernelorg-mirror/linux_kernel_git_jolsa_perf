#include "topology.h"
#include "tests.h"
#include "debug.h"

int test__topology(void)
{
	struct perf_tp tp;

	perf_tp__init(&tp);
	if (verbose) {
		fprintf(stdout, "\n");
		perf_tp__fprintf(stdout, &tp);
	}
	perf_tp__clean(&tp);
	return 0;
}
