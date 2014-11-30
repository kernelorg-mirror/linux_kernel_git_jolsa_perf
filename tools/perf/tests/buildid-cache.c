#include <api/fs/fs.h>
#include "tests.h"
#include "symbol.h"
#include "build-id.h"
#include "debug.h"
#include "exec_cmd.h"

static int add_kernel(void)
{
	char path[PATH_MAX];
	u8 build_id[BUILD_ID_SIZE];
	char sbuild_id[BUILD_ID_SIZE * 2 + 1];
	int ret;

	sprintf(path, "%s/kernel/notes", sysfs__mountpoint());
	ret = sysfs__read_build_id(path, build_id, sizeof(build_id));
	TEST_ASSERT_VAL("failed to get kernel buildid", !ret);

	build_id__sprintf(build_id, sizeof(build_id), sbuild_id);
	return build_id_cache__add_s(sbuild_id, buildid_dir, "[kernel.kallsyms]",
				     true, false);
}

static int __run_script(const char *script, const char *perf, char *cache)
{
	char cmd[PATH_MAX * 3 + 5];

	scnprintf(cmd, sizeof(cmd), "%s/buildid-cache.sh %s %s", script, perf, cache);
	return system(cmd);
}

static int run_script(char *cache)
{
	struct stat st;
	char path_perf[PATH_MAX];
	char path_script[PATH_MAX];

	/* First try developement tree tests. */
	if (!lstat("./tests", &st))
		return __run_script("./tests", "./perf", cache);

	/* Then installed path. */
	snprintf(path_script, PATH_MAX, "%s/tests", perf_exec_path());
	snprintf(path_perf, PATH_MAX, "%s/perf", BINDIR);

	if (!lstat(path_script, &st) && !lstat(path_perf, &st))
		return __run_script(path_script, path_perf, cache);

	fprintf(stderr, " (omitted)");
	return 0;
}

static int __test__buildid_cache(char *cache)
{
	set_buildid_dir(cache);

	TEST_ASSERT_VAL("failed to add [kernel.kallsyms] buildid",
			!add_kernel());

	TEST_ASSERT_VAL("script failed", !run_script(cache));
	return 0;
}

int test__buildid_cache(void)
{
	char cache[50];

	/*
	 * The directory removal is doen within
	 * __test__buildid_cache function.
	 */
	snprintf(cache, sizeof(cache), "/tmp/perf-XXXXXX");
	TEST_ASSERT_VAL("failed to make temp directory", mkdtemp(cache));

	pr_debug("buildid cache directory: %s\n", cache);

	return __test__buildid_cache(cache);
}
