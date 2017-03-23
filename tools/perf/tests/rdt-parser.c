#include <linux/compiler.h>
#include <stdio.h>
#include <string.h>
#include "tests.h"
#include "rdt.h"

int perf_rdt_parse(void *data);
extern FILE *perf_rdt_in;

extern int perf_rdt_debug;

int test__rdt_parser(int subtest __maybe_unused)
{
	static const char *test = "	\n\
resource L3 {				\n\
	cbm_mask     = 0x7ff		\n\
	min_cbm_bits = 1		\n\
	num_closids  = 16		\n\
	ids = {				\n\
		0=0-9,20-29		\n\
		1=10-19,30-39		\n\
	}				\n\
}					\n\
 					\n\
resource L2 {				\n\
	cbm_mask     = 0x1ff		\n\
	min_cbm_bits = 1		\n\
	num_closids  = 8		\n\
	ids = {				\n\
		2=0-9,20-29		\n\
		3=10-19,30-39		\n\
	}				\n\
}					\n\
 					\n\
group krava {				\n\
	cpus = 0-9			\n\
	schemata = {			\n\
		L3:0=1ff;1=1ff		\n\
		L2:0=1ff;1=1ff		\n\
	}				\n\
}					\n\
";
	FILE *file;
	struct rdt_data data;
	int ret;

	perf_rdt_debug = 1;

	file = fmemopen((void *) test, strlen(test), "r");
	if (!file)
		return -1;

	perf_rdt_in = file;
	ret = perf_rdt_parse(&data);
	fclose(file);
	return ret;
}
