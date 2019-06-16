#include <perf/evsel.h>
#include <linux/list.h>
#include <linux/string.h>
#include <internal/evsel.h>

static void perf_evsel__init(struct perf_evsel *evsel,
			     struct perf_event_attr *attr)
{
	INIT_LIST_HEAD(&evsel->node);
	if (attr)
		evsel->attr = *attr;
}

struct perf_evsel* perf_evsel__new_attr(struct perf_event_attr *attr)
{
	struct perf_evsel *evsel = zalloc(sizeof(*evsel));

	if (evsel != NULL)
		perf_evsel__init(evsel, attr);

       return evsel;
}

struct perf_evsel* perf_evsel__new(void)
{
	return perf_evsel__new_attr(NULL);
}

void perf_evsel__delete(struct perf_evsel *evsel)
{
	free(evsel);
}

void perf_evsel__set_priv(struct perf_evsel *evsel, void *priv)
{
	evsel->priv = priv;
}

void* perf_evsel__priv(struct perf_evsel *evsel)
{
	return evsel->priv;
}

struct perf_event_attr* perf_evsel__attr(struct perf_evsel *evsel)
{
	return &evsel->attr;
}
