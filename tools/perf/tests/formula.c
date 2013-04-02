
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <stdlib.h>
#include <unistd.h>
#include "tests.h"
#include "formula.h"
#include "evlist.h"
#include "evsel.h"

static void file_put(const char *file)
{
	unlink(file);
}

static char *file_get(const char *data, size_t size)
{
	char file[] = "/tmp/perf-tests-formula-XXXXXX";
	bool failed = false;
	int fd;

	fd = mkstemp(file);
	if (fd < 0) {
		pr_debug("failed to create temp file\n");
		return NULL;
	}

	if (size != (size_t) write(fd, data, size))
		failed = true;

	close(fd);

	if (failed)
		file_put(file);

	return failed ? NULL : file;
}

static int __test_basics(char *file)
{
	struct perf_formula fml;
	struct perf_formula_set *set;
	struct perf_formula_counter *counter;
	struct perf_evlist *evlist;
	struct perf_evsel *evsel;
	struct perf_counts *counts;
	int ret;

	pr_debug("file %s\n", file);

	perf_formula__init(&fml);

	ret = perf_formula__load(&fml, file);
	TEST_ASSERT_VAL("failed to load formula", !ret);

	set = perf_formula__set(&fml, (char *) "set");
	TEST_ASSERT_VAL("failed to load set", set);
	TEST_ASSERT_VAL("wrong set name", !strcmp(set->name, "set"));

	evlist = perf_evlist__new();
	TEST_ASSERT_VAL("failed to create evlist", evlist);

	ret = perf_formula__evlist(&fml, set, evlist);
	TEST_ASSERT_VAL("failed to load evlist with set", !ret);

	evsel = perf_evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
		PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);

	ret = perf_evsel__alloc_counts(evsel, 1);
	TEST_ASSERT_VAL("failed to alloc coiunts for evsel", !ret);

	counts = evsel->counts;
	counts->aggr.val = 1000;

	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
		PERF_COUNT_HW_INSTRUCTIONS == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);

	ret = perf_evsel__alloc_counts(evsel, 1);
	TEST_ASSERT_VAL("failed to alloc coiunts for evsel", !ret);

	counts = evsel->counts;
	counts->aggr.val = 500;

	perf_formula_set__eval(set, evlist, true);

	counter = list_first_entry(&set->head_counters,
				   struct perf_formula_counter,
				   list);
	TEST_ASSERT_VAL("failed to calculated the counter", !ret);
	TEST_ASSERT_VAL("wrong counter final value", counter->result->aggr.result == 4.5);

	counter = list_entry(counter->list.next,
			     struct perf_formula_counter,
			     list);
	TEST_ASSERT_VAL("failed to calculated the counter", !ret);
	TEST_ASSERT_VAL("wrong counter final value", counter->result->aggr.result == 2);

	perf_formula__free(&fml);
	return 0;
}

static int test_basics(void)
{
	int ret;
	char *file;
	char data[] =
"							\n\
set {							\n\
	events {					\n\
		CY = cycles:u				\n\
		IN = instructions:u			\n\
	}						\n\
							\n\
	t   = -IN / 1000 + 5				\n\
	cpi = CY / -((t - 5) * 1000)			\n\
							\n\
	print cpi					\n\
}							\n\
";

	file = file_get(data, sizeof(data));
	TEST_ASSERT_VAL("failed to get data file", file);

	ret = __test_basics(file);

	file_put(file);
	return ret;
}

typedef int (*fn_t)(void);

struct formula_test {
	const char *desc;
	fn_t fn;
} tests[] = {
	{
		.desc	= "basics",
		.fn	= test_basics,
	},
	{
		.desc	= NULL,
		.fn	= NULL,
	},
};


int test__formula(void)
{
	struct formula_test *t = &tests[0];
	int ret = 0;

	while (t->desc) {
		pr_debug("test: %s\n", t->desc);

		if (t->fn())
			ret = -1;

		t++;
	}

	return ret;
}
