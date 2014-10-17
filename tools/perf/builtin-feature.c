#include <linux/compiler.h>
#include "builtin.h"
#include "perf.h"
#include "parse-options.h"
#include "feat.h"
#include "color.h"
#include "debug.h"

#define PERF_FEATURE__ALL ((unsigned int) -1)

static bool display_all = true;

const struct option feature_options[] = {
	OPT_END()
};

static char *color_text(const char *text, const char *color)
{
	char buf[100];

	scnprintf(buf, 100, "%s%s%s", color, text, PERF_COLOR_RESET);
	return strdup(buf);
}

static void display_feature(unsigned int f)
{
	struct perf_feature *feat = pf_get(f);
	static char *yes, *no;

	if (!yes || !no) {
		yes = color_text("YES", PERF_COLOR_GREEN);
		no  = color_text("NO ", PERF_COLOR_RED);

		if (!yes || !no) {
			pr_err("Failed to initialize color text\n");
			return;
		}
	}

	fprintf(stdout, "%20s %20s %s %s\n", feat->name, feat->lib,
		feat->in     ? yes : no,
		feat->handle ? yes : no);
}

static void display(unsigned int f)
{
	unsigned int i;

	fprintf(stdout, "%20s %20s in  run\n", "feature", "");

	if (f != PERF_FEATURE__ALL) {
		display_feature(f);
	} else {
		for (i = 1; i < PERF_FEATURE__MAX; i++)
			display_feature(i);
	}
}

static const char * const feature_usage[] = {
	"perf feature",
	NULL
};

int cmd_feature(int argc, const char **argv, const char *prefix __maybe_unused)
{
	argc = parse_options(argc, argv, feature_options, feature_usage,
			    PARSE_OPT_STOP_AT_NON_OPTION);
	if (display_all)
		display(PERF_FEATURE__ALL);

	return 0;
}
