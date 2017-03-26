#include <linux/compiler.h>
#include <stdio.h>
#include <string.h>
#include "tests.h"
#include "rdt.h"

int perf_rdt_parse(void *data);
extern FILE *perf_rdt_in;

#ifdef PARSER_DEBUG
extern int perf_rdt_debug;
#endif

static int check_rdt_resource(struct rdt_data *data)
{
	struct rdt_resource *res;

	res = &data->resource[RDT_RESOURCE_L3];
	TEST_ASSERT_VAL("wrong L3 enabled",      res->enabled      == true);
	TEST_ASSERT_VAL("wrong L3 cbm_mask",     res->cbm_mask     == 0x7ff);
	TEST_ASSERT_VAL("wrong L3 min_cbm_bits", res->min_cbm_bits == 1);
	TEST_ASSERT_VAL("wrong L3 num_closids",  res->num_closids  == 16);

	res = &data->resource[RDT_RESOURCE_L2];
	TEST_ASSERT_VAL("wrong L2 enabled",      res->enabled      == true);
	TEST_ASSERT_VAL("wrong L2 cbm_mask",     res->cbm_mask     == 0x1ff);
	TEST_ASSERT_VAL("wrong L2 min_cbm_bits", res->min_cbm_bits == 2);
	TEST_ASSERT_VAL("wrong L2 num_closids",  res->num_closids  == 8);

	res = &data->resource[RDT_RESOURCE_L3DATA];
	TEST_ASSERT_VAL("wrong L3DATA enabled",  res->enabled      == false);

	res = &data->resource[RDT_RESOURCE_L3CODE];
	TEST_ASSERT_VAL("wrong L3CODE enabled",  res->enabled      == false);

	return 0;
}

static int check_rdt_group(struct rdt_data *data)
{
	struct rdt_group *g;

	g = list_first_entry(&data->groups, struct rdt_group, list);
	TEST_ASSERT_VAL("wrong name", !strcmp(g->name, "krava1"));
	TEST_ASSERT_VAL("wrong id", g->id == 1);

	g = list_next_entry(g, list);
	TEST_ASSERT_VAL("wrong name", !strcmp(g->name, "krava2"));
	TEST_ASSERT_VAL("wrong id", g->id == 2);

	return 0;
}

static int check_rdt_data(struct rdt_data *data)
{
	TEST_ASSERT_VAL("resource check failed", !check_rdt_resource(data));
	TEST_ASSERT_VAL("group check failed",    !check_rdt_group(data));

	return 0;
}

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
	min_cbm_bits = 2		\n\
	num_closids  = 8		\n\
	ids = {				\n\
		2=0-9,20-29		\n\
		3=10-19,30-39		\n\
	}				\n\
}					\n\
					\n\
group krava1 {				\n\
	id = 1				\n\
	cpus = 0-9			\n\
	schemata = {			\n\
		L3:0=1ff;1=1ff		\n\
		L2:0=1ff;1=1ff		\n\
	}				\n\
}					\n\
					\n\
group krava2 {				\n\
	id = 2				\n\
	cpus = 10-19			\n\
	schemata = {			\n\
		L2:0=f;1=f		\n\
		L3:0=1f0;1=1f0		\n\
	}				\n\
}					\n\
";
	FILE *file;
	struct rdt_data data;
	int ret;

#ifdef PARSER_DEBUG
	perf_rdt_debug = 1;
#endif

	memset(&data, 0, sizeof(data));
	INIT_LIST_HEAD(&data.groups);

	file = fmemopen((void *) test, strlen(test), "r");
	if (!file)
		return -1;

	perf_rdt_in = file;
	ret = perf_rdt_parse(&data);
	fclose(file);

	TEST_ASSERT_VAL("failed to parse rdt data", !ret);
	return check_rdt_data(&data);
}
