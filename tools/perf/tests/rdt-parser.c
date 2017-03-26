#include <linux/compiler.h>
#include <stdio.h>
#include <string.h>
#include "tests.h"
#include "rdt.h"

static int check_rdt_resource(struct rdt_data *data)
{
	struct rdt_resource *res;

	res = &data->resource[RDT_RESOURCE_L3];
	TEST_ASSERT_VAL("wrong L3 enabled",      res->enabled            == true);
	TEST_ASSERT_VAL("wrong L3 cbm_mask",     res->cache.cbm_mask     == 0x1fffff);
	TEST_ASSERT_VAL("wrong L3 min_cbm_bits", res->cache.min_cbm_bits == 1);
	TEST_ASSERT_VAL("wrong L3 num_closids",  res->num_closids        == 8);

	res = &data->resource[RDT_RESOURCE_L2];
	TEST_ASSERT_VAL("wrong L2 enabled",      res->enabled            == true);
	TEST_ASSERT_VAL("wrong L2 cbm_mask",     res->cache.cbm_mask     == 0x1fffff);
	TEST_ASSERT_VAL("wrong L2 min_cbm_bits", res->cache.min_cbm_bits == 1);
	TEST_ASSERT_VAL("wrong L2 num_closids",  res->num_closids        == 8);

	res = &data->resource[RDT_RESOURCE_L3DATA];
	TEST_ASSERT_VAL("wrong L3DATA enabled",  res->enabled      == false);

	res = &data->resource[RDT_RESOURCE_L3CODE];
	TEST_ASSERT_VAL("wrong L3CODE enabled",  res->enabled      == false);

	return 0;
}

static int check_rdt_group(struct rdt_data *data)
{
	struct rdt_group *g;
	struct rdt_schemata *schemata;
	char buf[100];
	int cnt = 0;

	list_for_each_entry(g, &data->groups, list)
		cnt++;

	TEST_ASSERT_VAL("wrong group count", cnt == 2);

	g = list_first_entry(&data->groups, struct rdt_group, list);
	TEST_ASSERT_VAL("wrong name", !strcmp(g->name, "default"));
	TEST_ASSERT_VAL("wrong id", g->id == 0);

	cpu_map__snprint(g->cpus, buf, 100);
	TEST_ASSERT_VAL("wrong id", !strcmp(buf, "1-23"));

	schemata = &g->schemata[RDT_RESOURCE_L3];
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[0].id   == 0);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[0].val  == 0x1fffff);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[1].id   == 1);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[1].val  == 0x1fffff);

	schemata = &g->schemata[RDT_RESOURCE_L2];
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[0].id   == 0);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[0].val  == 0x1fffff);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[1].id   == 1);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[1].val  == 0x1fffff);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[2].id   == 2);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[2].val  == 0x1fffff);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[3].id   == 3);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[3].val  == 0x1fffff);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[4].id   == 4);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[4].val  == 0x1fffff);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[5].id   == 5);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[5].val  == 0x1fffff);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[6].id   == 16);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[6].val  == 0x1fffff);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[7].id   == 17);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[7].val  == 0x1fffff);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[8].id   == 18);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[8].val  == 0x1fffff);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[9].id   == 19);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[9].val  == 0x1fffff);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[10].id  == 20);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[10].val == 0x1fffff);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[11].id  == 21);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[11].val == 0x1fffff);

	g = list_next_entry(g, list);
	TEST_ASSERT_VAL("wrong name", !strcmp(g->name, "krava"));
	TEST_ASSERT_VAL("wrong id", g->id == 1);

	cpu_map__snprint(g->cpus, buf, 100);
	TEST_ASSERT_VAL("wrong id", !strcmp(buf, "0"));

	schemata = &g->schemata[RDT_RESOURCE_L3];
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[0].id   == 0);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[0].val  == 0x1);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[1].id   == 1);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[1].val  == 0x1);

	schemata = &g->schemata[RDT_RESOURCE_L2];
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[0].id   == 0);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[0].val  == 0xf);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[1].id   == 1);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[1].val  == 0xf);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[2].id   == 2);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[2].val  == 0xf);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[3].id   == 3);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[3].val  == 0xf);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[4].id   == 4);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[4].val  == 0xf);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[5].id   == 5);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[5].val  == 0xf);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[6].id   == 16);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[6].val  == 0xf);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[7].id   == 17);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[7].val  == 0xf);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[8].id   == 18);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[8].val  == 0xf);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[9].id   == 19);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[9].val  == 0xf);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[10].id  == 20);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[10].val == 0xf);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[11].id  == 21);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[11].val == 0xf);
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
	static const char *test = "				\n\
{								\n\
	\"resources\" : [					\n\
		{						\n\
			\"name\"         : \"L3\",		\n\
			\"cbm_mask\"     : \"1fffff\",		\n\
			\"min_cbm_bits\" : \"1\",		\n\
			\"num_closids\"  : \"8\",		\n\
			\"ids\" : [				\n\
				{ \"  0\" : \"0-5,12-17\" },	\n\
				{ \"  1\" : \"6-11,18-23\" }	\n\
			]					\n\
		},						\n\
                {						\n\
                        \"name\"         : \"L2\",		\n\
                        \"cbm_mask\"     : \"1fffff\",		\n\
                        \"min_cbm_bits\" : \"1\",		\n\
                        \"num_closids\"  : \"8\",		\n\
                        \"ids\" : [				\n\
                                { \"0\"  : \"0,12\"  },		\n\
                                { \"1\"  : \"1,13\"  },		\n\
                                { \"2\"  : \"2,14\"  },		\n\
                                { \"3\"  : \"3,15\"  },		\n\
                                { \"4\"  : \"4,16\"  },		\n\
                                { \"5\"  : \"5,17\"  },		\n\
                                { \"16\" : \"6,18\"  },		\n\
                                { \"17\" : \"7,19\"  },		\n\
                                { \"18\" : \"8,20\"  },		\n\
                                { \"19\" : \"9,21\"  },		\n\
                                { \"20\" : \"10,22\" },		\n\
                                { \"21\" : \"11,23\" },		\n\
                        ],					\n\
                },						\n\
	],							\n\
        \"groups\" : [						\n\
		{						\n\
			\"name\"     : \"default\",		\n\
			\"id\"       : \"0\",			\n\
			\"cpus\"     : \"1-23\",		\n\
			\"schemata\" : [			\n\
				\"L3\" : [ 			\n\
					{ \"0\" : \"1fffff\" },	\n\
					{ \"1\" : \"1fffff\" }	\n\
				],				\n\
				\"L2\" : [			\n\
					{ \"0\"  : \"1fffff\" },\n\
					{ \"1\"  : \"1fffff\" },\n\
					{ \"2\"  : \"1fffff\" },\n\
					{ \"3\"  : \"1fffff\" },\n\
					{ \"4\"  : \"1fffff\" },\n\
					{ \"5\"  : \"1fffff\" },\n\
					{ \"16\" : \"1fffff\" },\n\
					{ \"17\" : \"1fffff\" },\n\
					{ \"18\" : \"1fffff\" },\n\
					{ \"19\" : \"1fffff\" },\n\
					{ \"20\" : \"1fffff\" },\n\
					{ \"21\" : \"1fffff\" }	\n\
				]				\n\
			]					\n\
		}						\n\
		{						\n\
			\"name\"     : \"krava\",		\n\
			\"id\"       : \"1\",			\n\
			\"cpus\"     : \"0\",			\n\
			\"schemata\" : [			\n\
				\"L3\" : [ 			\n\
					{ \"0\" : \"1\" },	\n\
					{ \"1\" : \"1\" }	\n\
				],				\n\
				\"L2\" : [			\n\
					{ \"0\"  : \"f\" },	\n\
					{ \"1\"  : \"f\" },	\n\
					{ \"2\"  : \"f\" },	\n\
					{ \"3\"  : \"f\" },	\n\
					{ \"4\"  : \"f\" },	\n\
					{ \"5\"  : \"f\" },	\n\
					{ \"16\" : \"f\" },	\n\
					{ \"17\" : \"f\" },	\n\
					{ \"18\" : \"f\" },	\n\
					{ \"19\" : \"f\" },	\n\
					{ \"20\" : \"f\" },	\n\
					{ \"21\" : \"f\" }	\n\
				]				\n\
			]					\n\
		}						\n\
	]							\n\
}								\n\
";
	struct rdt_data data;

	if (verbose > 1)
		pr_debug("parsing: %s\n", test);

	TEST_ASSERT_VAL("failed to parse rdt data", !rdt_parse_map(&data, strdup(test)));
	return check_rdt_data(&data);
}
