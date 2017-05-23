#include <linux/compiler.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <linux/limits.h>
#include <stdlib.h>
#include "util.h"
#include "tests.h"
#include "debug.h"
#include "rdt.h"
#include "cpumap.h"
#include "thread_map.h"

static int rdt_test(char *path)
{
	struct rdt_data data;
	struct rdt_resource *res;
	struct rdt_group *group;
	struct rdt_schemata *schemata;
	char str[100];

	if (rdt_load(&data, path))
		return TEST_FAIL;

	/* resource L3 */
	res = &data.resource[RDT_RESOURCE_L3];
	TEST_ASSERT_VAL("wrong enabled",      res->enabled == true);
	TEST_ASSERT_VAL("wrong num_closids",  res->num_closids == 16);
	TEST_ASSERT_VAL("wrong cbm_mask",     res->cache.cbm_mask == 0x7ff);
	TEST_ASSERT_VAL("wrong min_cbm_bits", res->cache.min_cbm_bits == 1);

	/* resource L3 */
	res = &data.resource[RDT_RESOURCE_MBA];
	TEST_ASSERT_VAL("wrong enabled",        res->enabled == true);
	TEST_ASSERT_VAL("wrong num_closids",    res->num_closids == 8);
	TEST_ASSERT_VAL("wrong bandwidth_gran", res->membw.bandwidth_gran == 10);
	TEST_ASSERT_VAL("wrong delay_linear",   res->membw.delay_linear == 1);
	TEST_ASSERT_VAL("wrong min_bandwidth",  res->membw.min_bandwidth == 10);

	/* group default */
	group = rdt_group__find(&data, 0);
	TEST_ASSERT_VAL("group not found",   group);

	TEST_ASSERT_VAL("wrong name",   !strcmp(group->name, "default"));
	TEST_ASSERT_VAL("wrong id",     group->id == 0);
	cpu_map__snprint(group->cpus, str, sizeof(str));
	TEST_ASSERT_VAL("wrong cpus",   !strcmp(str, "0-23"));
	thread_map__snprint(group->threads, str, sizeof(str));
	TEST_ASSERT_VAL("wrong tasks",  !strcmp(str, "0,1"));

	schemata = &group->schemata[RDT_RESOURCE_L3];
	TEST_ASSERT_VAL("wrong cnt", schemata->cnt == 4);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[0].id  == 0);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[0].val == 0x7ff);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[1].id  == 1);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[1].val == 0x7ff);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[2].id  == 2);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[2].val == 0x7ff);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[3].id  == 3);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[3].val == 0x7ff);

	schemata = &group->schemata[RDT_RESOURCE_MBA];
	TEST_ASSERT_VAL("wrong cnt", schemata->cnt == 4);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[0].id  == 0);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[0].val == 100);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[1].id  == 1);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[1].val == 100);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[2].id  == 2);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[2].val == 100);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[3].id  == 3);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[3].val == 100);

	/* group krava2 */
	group = rdt_group__find(&data, 2);
	TEST_ASSERT_VAL("group not found",   group);

	TEST_ASSERT_VAL("wrong name",   !strcmp(group->name, "krava2"));
	TEST_ASSERT_VAL("wrong id",     group->id == 2);
	cpu_map__snprint(group->cpus, str, sizeof(str));
	TEST_ASSERT_VAL("wrong cpus",   !strcmp(str, "13-23"));
	thread_map__snprint(group->threads, str, sizeof(str));
	TEST_ASSERT_VAL("wrong tasks",  !strcmp(str, "100,101"));

	schemata = &group->schemata[RDT_RESOURCE_L3];
	TEST_ASSERT_VAL("wrong cnt", schemata->cnt == 4);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[0].id  == 0);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[0].val == 0x7f0);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[1].id  == 1);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[1].val == 0x7f0);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[2].id  == 2);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[2].val == 0x7f0);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[3].id  == 3);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[3].val == 0x7f0);

	/* group krava1 */
	group = rdt_group__find(&data, 1);
	TEST_ASSERT_VAL("group not found",   group);

	TEST_ASSERT_VAL("wrong name",   !strcmp(group->name, "krava1"));
	TEST_ASSERT_VAL("wrong id",     group->id == 1);
	cpu_map__snprint(group->cpus, str, sizeof(str));
	TEST_ASSERT_VAL("wrong cpus",   !strcmp(str, "0-12"));
	thread_map__snprint(group->threads, str, sizeof(str));
	TEST_ASSERT_VAL("wrong tasks",  !strcmp(str, "10,11"));

	schemata = &group->schemata[RDT_RESOURCE_L3];
	TEST_ASSERT_VAL("wrong cnt", schemata->cnt == 4);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[0].id  == 0);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[0].val == 0xf);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[1].id  == 1);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[1].val == 0xf);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[2].id  == 2);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[2].val == 0xf);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[3].id  == 3);
	TEST_ASSERT_VAL("wrong cbm", schemata->cbm[3].val == 0xf);
	return 0;
}

int test__rdt_event(int subtest __maybe_unused)
{
	struct stat st;
	char path[PATH_MAX], script[PATH_MAX];
	int ret;

        snprintf(script, PATH_MAX, "./tests/rdt_event_setup.sh");
	if (lstat(script, &st))
		return TEST_SKIP;

        snprintf(path, PATH_MAX, "/tmp/perf-rdt-test-XXXXXX");
        if (!mkdtemp(path))
		return TEST_FAIL;

	pr_debug("temp dir: %s\n", path);

	snprintf(script, PATH_MAX, "./tests/rdt_event_setup.sh %s", path);

	ret = system(script);
	if (ret)
		goto out;

	ret = rdt_test(path);

out:
	rm_rf(path);
	return ret ? TEST_FAIL : TEST_OK;
}
