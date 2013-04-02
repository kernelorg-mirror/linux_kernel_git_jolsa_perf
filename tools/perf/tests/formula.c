
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <stdlib.h>
#include <unistd.h>
#include "tests.h"
#include "formula.h"
#include "evlist.h"
#include "evsel.h"
#include "util.h"
#include "debug.h"

static void file_put(const char *file)
{
	unlink(file);
}

static char *file_get(const char *data, size_t size)
{
	static char file[] = "/tmp/perf-tests-formula-XXXXXX";
	bool failed = false;
	int fd;

	fd = mkstemp(file);
	if (fd < 0) {
		pr_debug("failed to create temp file\n");
		return NULL;
	}

	if (size != (size_t) writen(fd, (void *) data, size))
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
	int ret;

	pr_debug("file %s\n", file);

	perf_formula__init(&fml);

	ret = perf_formula__load(&fml, file);
	TEST_ASSERT_VAL("failed to load formula", !ret);

	set = perf_formula__set(&fml, (char *) "set");
	TEST_ASSERT_VAL("failed to load set", set);
	TEST_ASSERT_VAL("wrong set name", !strcmp(set->name, "set"));

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
