#include <perf/evsel.h>
#include <linux/list.h>
#include <linux/string.h>
#include <internal/evsel.h>

static void perf_evsel__init(struct perf_evsel *evsel)
{
	INIT_LIST_HEAD(&evsel->node);
}

struct perf_evsel* perf_evsel__new(void)
{
	struct perf_evsel *evsel = zalloc(sizeof(*evsel));

	if (evsel != NULL)
		perf_evsel__init(evsel);

       return evsel;
}

void perf_evsel__delete(struct perf_evsel *evsel)
{
	free(evsel);
}
