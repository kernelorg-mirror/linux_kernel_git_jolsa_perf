#include <perf/evlist.h>
#include <linux/list.h>
#include <linux/string.h>
#include <internal/evlist.h>

static void perf_evlist__init(struct perf_evlist *evlist)
{
	INIT_LIST_HEAD(&evlist->entries);
}

struct perf_evlist* perf_evlist__new(void)
{
	struct perf_evlist *evlist = zalloc(sizeof(*evlist));

	if (evlist != NULL)
		perf_evlist__init(evlist);

	return evlist;
}

void perf_evlist__delete(struct perf_evlist *evlist)
{
	free(evlist);
}

void perf_evlist__set_priv(struct perf_evlist *evlist, void *priv)
{
	evlist->priv = priv;
}

void* perf_evlist__priv(struct perf_evlist *evlist)
{
	return evlist->priv;
}
