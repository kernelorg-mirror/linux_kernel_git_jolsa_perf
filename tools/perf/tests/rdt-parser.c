#include <linux/compiler.h>
#include <stdio.h>
#include "tests.h"
#include "rdt.h"

int perf_rdt_parse(void *data);
extern FILE *perf_rdt_in;

int test__rdt_parser(int subtest __maybe_unused)
{
	static const char *test = "	\
resource L3				\n\
group krava				\n\
";
	FILE *file;
	struct perf_rdt_data data;
	int ret;

	file = fmemopen((void *) test, sizeof(test), "r");
	if (!file)
		return -1;

	perf_rdt_in = file;
	ret = perf_rdt_parse(&data);
	fclose(file);
	return ret;
}
