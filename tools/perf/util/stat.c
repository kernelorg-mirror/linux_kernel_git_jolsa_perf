#include <math.h>
#include <linux/bitmap.h>
#include "evsel.h"
#include "stat.h"
#include "thread_map.h"

enum aggr_mode aggr_mode = AGGR_GLOBAL;
bool perf_stat_scale	 = true;

void update_stats(struct stats *stats, u64 val)
{
	double delta;

	stats->n++;
	delta = val - stats->mean;
	stats->mean += delta / stats->n;
	stats->M2 += delta*(val - stats->mean);

	if (val > stats->max)
		stats->max = val;

	if (val < stats->min)
		stats->min = val;
}

double avg_stats(struct stats *stats)
{
	return stats->mean;
}

/*
 * http://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
 *
 *       (\Sum n_i^2) - ((\Sum n_i)^2)/n
 * s^2 = -------------------------------
 *                  n - 1
 *
 * http://en.wikipedia.org/wiki/Stddev
 *
 * The std dev of the mean is related to the std dev by:
 *
 *             s
 * s_mean = -------
 *          sqrt(n)
 *
 */
double stddev_stats(struct stats *stats)
{
	double variance, variance_mean;

	if (stats->n < 2)
		return 0.0;

	variance = stats->M2 / (stats->n - 1);
	variance_mean = variance / stats->n;

	return sqrt(variance_mean);
}

double rel_stddev_stats(double stddev, double avg)
{
	double pct = 0.0;

	if (avg)
		pct = 100.0 * stddev/avg;

	return pct;
}

static void zero_per_pkg(struct perf_evsel *counter)
{
	if (counter->per_pkg_mask)
		memset(counter->per_pkg_mask, 0, MAX_NR_CPUS);
}

static int check_per_pkg(struct perf_evsel *counter, int cpu, bool *skip)
{
	unsigned long *mask = counter->per_pkg_mask;
	struct cpu_map *cpus = perf_evsel__cpus(counter);
	int s;

	*skip = false;

	if (!counter->per_pkg)
		return 0;

	if (cpu_map__empty(cpus))
		return 0;

	if (!mask) {
		mask = zalloc(MAX_NR_CPUS);
		if (!mask)
			return -ENOMEM;

		counter->per_pkg_mask = mask;
	}

	s = cpu_map__get_socket(cpus, cpu);
	if (s < 0)
		return -1;

	*skip = test_and_set_bit(s, mask) == 1;
	return 0;
}

static int
process_counter_values(struct perf_evsel *evsel, int cpu, int thread,
		       struct perf_counts_values *count)
{
	struct perf_counts_values *aggr = &evsel->counts->aggr;
	static struct perf_counts_values zero;
	bool skip = false;

	if (check_per_pkg(evsel, cpu, &skip)) {
		pr_err("failed to read per-pkg counter\n");
		return -1;
	}

	if (skip)
		count = &zero;

	switch (aggr_mode) {
	case AGGR_CORE:
	case AGGR_SOCKET:
	case AGGR_NONE:
		if (!evsel->snapshot)
			perf_evsel__compute_deltas(evsel, cpu, thread, count);
		perf_counts_values__scale(count, perf_stat_scale, NULL);
		if (aggr_mode == AGGR_NONE)
			perf_stat__update_shadow_stats(evsel, count->values, cpu);
		break;
	case AGGR_GLOBAL:
		aggr->val += count->val;
		if (perf_stat_scale) {
			aggr->ena += count->ena;
			aggr->run += count->run;
		}
	default:
		break;
	}

	return 0;
}

static int process_counter_maps(struct perf_evsel *counter)
{
	int nthreads = thread_map__nr(counter->threads);
	int ncpus = perf_evsel__nr_cpus(counter);
	int cpu, thread;

	if (counter->system_wide)
		nthreads = 1;

	for (thread = 0; thread < nthreads; thread++) {
		for (cpu = 0; cpu < ncpus; cpu++) {
			if (process_counter_values(counter, cpu, thread,
						   perf_counts(counter->counts, cpu, thread)))
				return -1;
		}
	}

	return 0;
}

int perf_stat__process_counter(struct perf_evsel *counter)
{
	struct perf_counts_values *aggr = &counter->counts->aggr;
	struct perf_stat *ps = counter->priv;
	u64 *count = counter->counts->aggr.values;
	int i, ret;

	aggr->val = aggr->ena = aggr->run = 0;
	memset(ps->res_stats, 0, sizeof(ps->res_stats));

	if (counter->per_pkg)
		zero_per_pkg(counter);

	ret = process_counter_maps(counter);
	if (ret)
		return ret;

	if (aggr_mode != AGGR_GLOBAL)
		return 0;

	if (!counter->snapshot)
		perf_evsel__compute_deltas(counter, -1, -1, aggr);
	perf_counts_values__scale(aggr, perf_stat_scale, &counter->counts->scaled);

	for (i = 0; i < 3; i++)
		update_stats(&ps->res_stats[i], count[i]);

	if (verbose) {
		fprintf(stderr, "%s: %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
			perf_evsel__name(counter), count[0], count[1], count[2]);
	}

	/*
	 * Save the full runtime - to allow normalization during printout:
	 */
	perf_stat__update_shadow_stats(counter, count, 0);

	return 0;
}
