#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/stringify.h>
#include "util.h"
#include "debug.h"
#include "builtin.h"
#include <subcmd/parse-options.h>
#include "mem-events.h"
#include "session.h"
#include "hist.h"
#include "sort.h"
#include "tool.h"
#include "data.h"
#include "sort.h"
#include "ui/browsers/hists.h"

struct c2c_hists {
	struct hists		hists;
	struct perf_hpp_list	list;
	struct c2c_stats	stats;
};

struct c2c_hist_entry {
	struct c2c_hists	*hists;
	struct c2c_stats	stats;
	/*
	 * must be at the end,
	 * because of its callchain dynamic entry
	 */
	struct hist_entry	he;
};

#define HAS_HITMS(__h) (__h->stats.t.lcl_hitm || __h->stats.t.rmt_hitm)

struct perf_c2c {
	struct perf_tool	tool;
	struct c2c_hists	hists;
	bool			stats_only;

	/* HITM shared clines stats */
	struct c2c_stats	hitm_stats;
	int			shared_clines;
};

static struct perf_c2c c2c;

static void* c2c_he_zalloc(size_t size)
{
	struct c2c_hist_entry *c2c_he;

	c2c_he = zalloc(size + sizeof(*c2c_he));
	if (!c2c_he)
		return NULL;

	return &c2c_he->he;
}

static void c2c_he_free(void *he)
{
	struct c2c_hist_entry *c2c_he;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	if (c2c_he->hists) {
		hists__delete_entries(&c2c_he->hists->hists);
		free(c2c_he->hists);
	}

	free(c2c_he);
}

static struct hist_entry_ops c2c_entry_ops = {
	.new	= c2c_he_zalloc,
	.free	= c2c_he_free,
};

static int c2c_hists__init(struct c2c_hists *hists,
			   const char *sort);

static struct c2c_hists*
he__get_c2c_hists(struct hist_entry *he,
		  const char *sort)
{
	struct c2c_hist_entry *c2c_he;
	struct c2c_hists *hists;
	int ret;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	if (c2c_he->hists)
		return c2c_he->hists;

	hists = c2c_he->hists = zalloc(sizeof(*hists));
	if (!hists)
		return NULL;

	ret = c2c_hists__init(hists, sort);
	if (ret)
		free(hists);

	return hists;
}

static int process_sample_event(struct perf_tool *tool __maybe_unused,
				union perf_event *event,
				struct perf_sample *sample,
				struct perf_evsel *evsel __maybe_unused,
				struct machine *machine)
{
	struct hists *hists = &c2c.hists.hists;
	struct c2c_hist_entry *c2c_he;
	struct hist_entry *he;
	struct addr_location al;
	struct mem_info *mi, *mi_dup;
	int ret;

	if (machine__resolve(machine, &al, sample) < 0) {
		pr_debug("problem processing %d event, skipping it.\n",
			 event->header.type);
		return -1;
	}

	mi = sample__resolve_mem(sample, &al);
	if (mi == NULL)
		return -ENOMEM;

	mi_dup = memdup(mi, sizeof(*mi));
	if (!mi_dup)
		goto free_mi;

	he = hists__add_entry_ops(hists, &c2c_entry_ops,
				  &al, NULL, NULL, mi,
				  sample, true);
	if (he == NULL)
		goto free_mi_dup;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	c2c_decode_stats(&c2c_he->stats, he);
	c2c_decode_stats(&c2c.hists.stats, he);

	hists__inc_nr_samples(hists, he->filtered);
	ret = hist_entry__append_callchain(he, sample);

	if (!ret) {
		struct c2c_hists *c2c_hists;

		mi = mi_dup;

		mi_dup = memdup(mi, sizeof(*mi));
		if (!mi_dup)
			goto free_mi;

		c2c_hists = he__get_c2c_hists(he, "offset");
		if (!c2c_hists)
			goto free_mi_dup;

		he = hists__add_entry_ops(&c2c_hists->hists, &c2c_entry_ops,
					  &al, NULL, NULL, mi,
					  sample, true);
		if (he == NULL)
			goto free_mi_dup;

		c2c_he = container_of(he, struct c2c_hist_entry, he);
		c2c_decode_stats(&c2c_he->stats, he);

		hists__inc_nr_samples(&c2c_hists->hists, he->filtered);
		ret = hist_entry__append_callchain(he, sample);
	}

out:
	addr_location__put(&al);
	return ret;

free_mi_dup:
	free(mi_dup);
free_mi:
	free(mi);
	ret = -ENOMEM;
	goto out;
}

static struct perf_c2c c2c = {
	.tool = {
		.sample		= process_sample_event,
		.mmap		= perf_event__process_mmap,
		.mmap2		= perf_event__process_mmap2,
		.comm		= perf_event__process_comm,
		.exit		= perf_event__process_exit,
		.fork		= perf_event__process_fork,
		.lost		= perf_event__process_lost,
		.ordered_events	= true,
		.ordering_requires_timestamps = true,
	},
};

static const char * const c2c_usage[] = {
	"perf c2c {record|report}",
	NULL
};

static const char * const __usage_report[] = {
	"perf c2c report",
	NULL
};

static const char * const *report_c2c_usage = __usage_report;

struct c2c_dimension {
	struct perf_hpp_header phh[PERF_HPP_HEADER_MAX];
	const char *name;
	int64_t (*cmp)(struct perf_hpp_fmt *fmt,
		       struct hist_entry *, struct hist_entry *);
	int (*entry)(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
                             struct hist_entry *he);
	int id;
	int width;
};

struct c2c_fmt {
	struct perf_hpp_fmt	 fmt;
	struct c2c_dimension	*dim;
};

static int c2c_width(struct perf_hpp_fmt *fmt,
		     struct perf_hpp *hpp __maybe_unused,
		     struct hists *hists __maybe_unused)
{
	struct c2c_fmt *c2c_fmt;

	c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	return c2c_fmt->dim->width;
}

static int c2c_header(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		      struct hists *hists __maybe_unused, int line, bool *defined, int *span)
{
        int len = c2c_width(fmt, hpp, hists);
        const char *text = perf_hpp_header_n(fmt, line).text;

        *defined = text != NULL;
        if (!*defined)
                text = "";

	if (*span) {
		text = "-";
		(*span)--;
	} else {
		*span = perf_hpp_header_n(fmt, line).span;
	}

        return scnprintf(hpp->buf, hpp->size, "%*s", len, text);
}

static int64_t
dcacheline_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	       struct hist_entry *left, struct hist_entry *right)
{
	u64 l, r;

	if (!left->mem_info)  return -1;
	if (!right->mem_info) return 1;

	/* al_addr does all the right addr - start + offset calculations */
	l = cl_address(left->mem_info->daddr.addr);
	r = cl_address(right->mem_info->daddr.addr);

	if (l > r) return -1;
	if (l < r) return 1;

	return 0;
}

static int dcacheline_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			    struct hist_entry *he)
{
	uint64_t addr = 0;
	int width = c2c_width(fmt, hpp, he->hists);

	if (he->mem_info)
		addr = cl_address(he->mem_info->daddr.addr);

	return snprintf(hpp->buf, hpp->size, "%*" PRIx64, width, addr);
}

static int offset_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			struct hist_entry *he)
{
	uint64_t addr = 0;
	int width = c2c_width(fmt, hpp, he->hists);

	if (he->mem_info)
		addr = cl_offset(he->mem_info->daddr.addr);

	return snprintf(hpp->buf, hpp->size, "0x%-*" PRIx64, width, addr);
}

static int64_t
daddr_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	   struct hist_entry *left, struct hist_entry *right)
{
	u64 l, r;

	if (!left->mem_info)  return -1;
	if (!right->mem_info) return 1;

	/* al_addr does all the right addr - start + offset calculations */
	l = left->mem_info->daddr.addr;
	r = right->mem_info->daddr.addr;

	if (l > r) return -1;
	if (l < r) return 1;

	return 0;
}

static int daddr_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he)
{
	uint64_t addr = 0;
	int width = c2c_width(fmt, hpp, he->hists);

	if (he->mem_info)
		addr = he->mem_info->daddr.al_addr;

	return snprintf(hpp->buf, hpp->size, "%*" PRIx64, width, addr);
}

static int
iaddr_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	    struct hist_entry *he)
{
	uint64_t addr = 0;
	int width = c2c_width(fmt, hpp, he->hists);

	if (he->mem_info)
		addr = he->mem_info->iaddr.addr;

	return snprintf(hpp->buf, hpp->size, "%*" PRIx64, width, addr);
}

static int64_t
iaddr_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	  struct hist_entry *left, struct hist_entry *right)
{
	u64 l, r;

	if (!left->mem_info)  return -1;
	if (!right->mem_info) return 1;

	/* al_addr does all the right addr - start + offset calculations */
	l = left->mem_info->iaddr.addr;
	r = right->mem_info->iaddr.addr;

	if (l > r) return -1;
	if (l < r) return 1;

	return 0;
}

static int
tot_hitm_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	       struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	unsigned int tot_hitm;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	tot_hitm = c2c_he->stats.t.lcl_hitm + c2c_he->stats.t.rmt_hitm;

	return snprintf(hpp->buf, hpp->size, "%*u", width, tot_hitm);
}

static int
lcl_hitm_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	       struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width,
			c2c_he->stats.t.lcl_hitm);
}

static int
rmt_hitm_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	       struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width,
			c2c_he->stats.t.rmt_hitm);
}

static int64_t
tot_hitm_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;
	unsigned int tot_hitm_left;
	unsigned int tot_hitm_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	tot_hitm_left  = c2c_left->stats.t.lcl_hitm + c2c_left->stats.t.rmt_hitm;
	tot_hitm_right = c2c_right->stats.t.lcl_hitm + c2c_right->stats.t.rmt_hitm;
	return tot_hitm_left - tot_hitm_right;
}

static int64_t
lcl_hitm_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.t.lcl_hitm - c2c_right->stats.t.lcl_hitm;
}

static int64_t
rmt_hitm_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.t.rmt_hitm - c2c_right->stats.t.rmt_hitm;
}

static int
stores_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	     struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width, c2c_he->stats.t.store);
}

static int
stores_l1hit_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		   struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width, c2c_he->stats.t.st_l1hit);
}

static int
stores_l1miss_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		    struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width, c2c_he->stats.t.st_l1miss);
}

static int64_t
stores_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	   struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.t.store - c2c_right->stats.t.store;
}

static int64_t
stores_l1hit_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
		 struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.t.st_l1hit - c2c_right->stats.t.st_l1hit;
}

static int64_t
stores_l1miss_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
		  struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.t.st_l1miss - c2c_right->stats.t.st_l1miss;
}

static int
ld_fbhit_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	       struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width, c2c_he->stats.t.ld_fbhit);
}

static int
ld_l1hit_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	       struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width, c2c_he->stats.t.ld_l1hit);
}

static int
ld_l2hit_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	       struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width, c2c_he->stats.t.ld_l2hit);
}

static int64_t
ld_fbhit_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.t.ld_fbhit - c2c_right->stats.t.ld_fbhit;
}

static int64_t
ld_l1hit_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.t.ld_l1hit - c2c_right->stats.t.ld_l1hit;
}

static int64_t
ld_l2hit_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.t.ld_l2hit - c2c_right->stats.t.ld_l2hit;
}

static int
ld_llchit_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width, c2c_he->stats.t.ld_llchit);
}

static int
ld_rmthit_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width, c2c_he->stats.t.rmt_hit);
}

static int64_t
ld_llchit_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.t.ld_llchit - c2c_right->stats.t.ld_llchit;
}

static int64_t
ld_rmthit_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.t.rmt_hit - c2c_right->stats.t.rmt_hit;
}

static uint64_t llc_miss(struct c2c_stats *stats)
{
	uint64_t llcmiss;

	llcmiss = stats->t.lcl_dram +
		  stats->t.rmt_dram +
		  stats->t.rmt_hitm +
		  stats->t.rmt_hit;

	return llcmiss;
}

static int
ld_llcmiss_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		 struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*lu", width, llc_miss(&c2c_he->stats));
}

static int64_t
ld_llcmiss_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	       struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return llc_miss(&c2c_left->stats) - llc_miss(&c2c_right->stats);
}

static uint64_t total_records(struct c2c_stats *stats)
{
	uint64_t lclmiss, ldcnt, total;

	lclmiss  = stats->t.lcl_dram +
		   stats->t.rmt_dram +
		   stats->t.rmt_hitm +
		   stats->t.rmt_hit;

	ldcnt    = lclmiss +
		   stats->t.ld_fbhit +
		   stats->t.ld_l1hit +
		   stats->t.ld_l2hit +
		   stats->t.ld_llchit +
		   stats->t.lcl_hitm;

	total    = ldcnt +
		   stats->t.st_l1hit +
		   stats->t.st_l1miss;

	return total;
}

static int
tot_recs_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	uint64_t tot_recs;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	tot_recs = total_records(&c2c_he->stats);

	return snprintf(hpp->buf, hpp->size, "%*" PRIu64, width, tot_recs);
}

static int64_t
tot_recs_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;
	uint64_t tot_recs_left;
	uint64_t tot_recs_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	tot_recs_left  = total_records(&c2c_left->stats);
	tot_recs_right = total_records(&c2c_right->stats);

	return tot_recs_left - tot_recs_right;
}

static uint64_t total_loads(struct c2c_stats *stats)
{
	uint64_t lclmiss, ldcnt;

	lclmiss  = stats->t.lcl_dram +
		   stats->t.rmt_dram +
		   stats->t.rmt_hitm +
		   stats->t.rmt_hit;

	ldcnt    = lclmiss +
		   stats->t.ld_fbhit +
		   stats->t.ld_l1hit +
		   stats->t.ld_l2hit +
		   stats->t.ld_llchit +
		   stats->t.lcl_hitm;

	return ldcnt;
}

static int
tot_loads_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	uint64_t tot_recs;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	tot_recs = total_loads(&c2c_he->stats);

	return snprintf(hpp->buf, hpp->size, "%*" PRIu64, width, tot_recs);
}

static int64_t
tot_loads_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	      struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;
	uint64_t tot_recs_left;
	uint64_t tot_recs_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	tot_recs_left  = total_loads(&c2c_left->stats);
	tot_recs_right = total_loads(&c2c_right->stats);

	return tot_recs_left - tot_recs_right;
}

static double percent_hitm(struct c2c_hist_entry *c2c_he)
{
	struct c2c_hists *hists;
	struct c2c_stats *stats;
	struct c2c_stats *total;
	int tot, st;
	double p;

	hists = container_of(c2c_he->he.hists, struct c2c_hists, hists);
	stats = &c2c_he->stats;
	total = &hists->stats;

	st  = stats->t.lcl_hitm + stats->t.rmt_hitm;
	tot = total->t.lcl_hitm + total->t.rmt_hitm;

	p = tot ? (double) st / tot : 0;

	return 100 * p;
}

static int
percent_hitm_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		   struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	char buf[10];
	double per;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	per = percent_hitm(c2c_he);

	snprintf(buf, 10, "%.2F%%", per);
	return snprintf(hpp->buf, hpp->size, "%*s", width, buf);
}

static int64_t
percent_hitm_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
		 struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;
	double per_left;
	double per_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	per_left  = percent_hitm(c2c_left);
	per_right = percent_hitm(c2c_right);

	return per_left - per_right;
}

static double percent_ldmiss(struct c2c_hist_entry *c2c_he)
{
	struct c2c_hists *hists;
	struct c2c_stats *stats;
	struct c2c_stats *total;
	int tot, st;
	double p;

	hists = container_of(c2c_he->he.hists, struct c2c_hists, hists);
	stats = &c2c_he->stats;
	total = &hists->stats;

	st  = stats->t.rmt_hitm;
        tot = total->t.lcl_dram +
              total->t.rmt_dram +
              total->t.rmt_hit +
              total->t.rmt_hitm;

	p = tot ? (double) st / tot : 0;

	return 100 * p;
}

static int
percent_ldmiss_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		     struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	char buf[10];
	double per;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	per = percent_hitm(c2c_he);

	snprintf(buf, 10, "%.2F%%", per);
	return snprintf(hpp->buf, hpp->size, "%*s", width, buf);
}

static int64_t
percent_ldmiss_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
		   struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;
	double per_left;
	double per_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	per_left  = percent_ldmiss(c2c_left);
	per_right = percent_ldmiss(c2c_right);

	return per_left - per_right;
}

static struct c2c_stats *he_stats(struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	return &c2c_he->stats;
}

static struct c2c_stats *total_stats(struct hist_entry *he)
{
	struct c2c_hists *hists;

	hists = container_of(he->hists, struct c2c_hists, hists);
	return &hists->stats;
}

static double percent(int st, int tot)
{
	return tot ? 100. * (double) st / (double) tot : 0;
}

#define PERCENT(__h, __f) percent(he_stats(__h)->t.__f, total_stats(__h)->t.__f)

static int
percent_rmt_hitm_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);
	double per = PERCENT(he, rmt_hitm);

	return snprintf(hpp->buf, hpp->size, "%*F", width, per);
}

static int64_t
percent_rmt_hitm_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
		     struct hist_entry *left, struct hist_entry *right)
{
	double per_left;
	double per_right;

	per_left  = PERCENT(left, lcl_hitm);
	per_right = PERCENT(right, lcl_hitm);

	return per_left - per_right;
}

static int
percent_lcl_hitm_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);
	double per = PERCENT(he, lcl_hitm);

	return snprintf(hpp->buf, hpp->size, "%*F", width, per);
}

static int64_t
percent_lcl_hitm_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
		     struct hist_entry *left, struct hist_entry *right)
{
	double per_left;
	double per_right;

	per_left  = PERCENT(left, lcl_hitm);
	per_right = PERCENT(right, lcl_hitm);

	return per_left - per_right;
}

static int
percent_stores_l1hit_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			   struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);
	double per = PERCENT(he, st_l1hit);

	return snprintf(hpp->buf, hpp->size, "%*F", width, per);
}

static int64_t
percent_stores_l1hit_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
			struct hist_entry *left, struct hist_entry *right)
{
	double per_left;
	double per_right;

	per_left  = PERCENT(left, st_l1hit);
	per_right = PERCENT(right, st_l1hit);

	return per_left - per_right;
}

static int
percent_stores_l1miss_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			   struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);
	double per = PERCENT(he, st_l1miss);

	return snprintf(hpp->buf, hpp->size, "%*F", width, per);
}

static int64_t
percent_stores_l1miss_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
			  struct hist_entry *left, struct hist_entry *right)
{
	double per_left;
	double per_right;

	per_left  = PERCENT(left, st_l1miss);
	per_right = PERCENT(right, st_l1miss);

	return per_left - per_right;
}

enum {
	DIM_DCACHELINE,
	DIM_OFFSET,
	DIM_DADDR,
	DIM_IADDR,
	DIM_TOT_HITM,
	DIM_LCL_HITM,
	DIM_RMT_HITM,
	DIM_STORES,
	DIM_STORES_L1HIT,
	DIM_STORES_L1MISS,
	DIM_LD_FBHIT,
	DIM_LD_L1HIT,
	DIM_LD_L2HIT,
	DIM_LD_LLC_HIT,
	DIM_LD_RMT_HIT,
	DIM_LD_LLC_MISS,
	DIM_TOT_RECS,
	DIM_TOT_LOADS,
	DIM_PERCENT_HITM,
	DIM_PERCENT_LDMISS,
	DIM_PERCENT_LCL_HITM,
	DIM_PERCENT_RMT_HITM,
	DIM_PERCENT_STORES_L1HIT,
	DIM_PERCENT_STORES_L1MISS,
};

#define HEADER(__h)			\
	.phh[PERF_HPP_HEADER_1] = {	\
		.text = __h,		\
	}

#define HEADER2(__h2, __h1)		\
	.phh[PERF_HPP_HEADER_2] = {	\
		.text = # __h2,		\
	},				\
	.phh[PERF_HPP_HEADER_1] = {	\
		.text = # __h1,		\
	}

static struct c2c_dimension dim_dcacheline = {
	HEADER("Cacheline"),
	.name		= "dcacheline",
	.cmp		= dcacheline_cmp,
	.entry		= dcacheline_entry,
	.id		= DIM_DCACHELINE,
};

static struct c2c_dimension dim_offset = {
	HEADER("Off"),
	.name		= "offset",
	.cmp		= daddr_cmp,
	.entry		= offset_entry,
	.id		= DIM_OFFSET,
};

static struct c2c_dimension dim_daddr = {
	HEADER("Data address"),
	.name		= "daddr",
	.cmp		= daddr_cmp,
	.entry		= daddr_entry,
	.id		= DIM_DADDR,
};

static struct c2c_dimension dim_iaddr = {
	HEADER("Code address"),
	.name		= "iaddr",
	.cmp		= iaddr_cmp,
	.entry		= iaddr_entry,
	.id		= DIM_IADDR,
};

static struct c2c_dimension dim_tot_hitm = {
	HEADER2(Tot, HITM),
	.name		= "tot_hitm",
	.cmp		= tot_hitm_cmp,
	.entry		= tot_hitm_entry,
	.id		= DIM_TOT_HITM,
};

static struct c2c_dimension dim_lcl_hitm = {
	HEADER2(Lcl, HITM),
	.name		= "lcl_hitm",
	.cmp		= lcl_hitm_cmp,
	.entry		= lcl_hitm_entry,
	.id		= DIM_LCL_HITM,
};

static struct c2c_dimension dim_rmt_hitm = {
	HEADER2(Rmt, HITM),
	.name		= "rmt_hitm",
	.cmp		= rmt_hitm_cmp,
	.entry		= rmt_hitm_entry,
	.id		= DIM_RMT_HITM,
};

static struct c2c_dimension dim_stores = {
	HEADER("Stores"),
	.name		= "stores",
	.cmp		= stores_cmp,
	.entry		= stores_entry,
	.id		= DIM_STORES,
};

static struct c2c_dimension dim_stores_l1hit = {
	HEADER2(Stores, L1Hit),
	.name		= "stores_l1hit",
	.cmp		= stores_l1hit_cmp,
	.entry		= stores_l1hit_entry,
	.id		= DIM_STORES_L1HIT,
};

static struct c2c_dimension dim_stores_l1miss = {
	HEADER2(Stores, L1Miss),
	.name		= "stores_l1miss",
	.cmp		= stores_l1miss_cmp,
	.entry		= stores_l1miss_entry,
	.id		= DIM_STORES_L1MISS,
};

static struct c2c_dimension dim_ld_fbhit = {
	HEADER2(Load, FBHit),
	.name		= "ld_fbhit",
	.cmp		= ld_fbhit_cmp,
	.entry		= ld_fbhit_entry,
	.id		= DIM_LD_FBHIT,
};

static struct c2c_dimension dim_ld_l1hit = {
	HEADER2(Load, L1Hit),
	.name		= "ld_l1hit",
	.cmp		= ld_l1hit_cmp,
	.entry		= ld_l1hit_entry,
	.id		= DIM_LD_L1HIT,
};

static struct c2c_dimension dim_ld_l2hit = {
	HEADER2(Load, L2Hit),
	.name		= "ld_l2hit",
	.cmp		= ld_l2hit_cmp,
	.entry		= ld_l2hit_entry,
	.id		= DIM_LD_L2HIT,
};

static struct c2c_dimension dim_ld_llchit = {
	HEADER2(Load, LlcHit),
	.name		= "ld_lclhit",
	.cmp		= ld_llchit_cmp,
	.entry		= ld_llchit_entry,
	.id		= DIM_LD_LLC_HIT,
};

static struct c2c_dimension dim_ld_rmthit = {
	HEADER2(Load, RmtHit),
	.name		= "ld_rmthit",
	.cmp		= ld_rmthit_cmp,
	.entry		= ld_rmthit_entry,
	.id		= DIM_LD_RMT_HIT,
};

static struct c2c_dimension dim_ld_llcmiss = {
	HEADER2(LLC, Ld Miss),
	.name		= "ld_llcmiss",
	.cmp		= ld_llcmiss_cmp,
	.entry		= ld_llcmiss_entry,
	.id		= DIM_LD_LLC_MISS,
};

static struct c2c_dimension dim_tot_recs = {
	HEADER2(Total, records),
	.name		= "tot_recs",
	.cmp		= tot_recs_cmp,
	.entry		= tot_recs_entry,
	.id		= DIM_TOT_RECS,
};

static struct c2c_dimension dim_tot_loads = {
	HEADER2(Total, Loads),
	.name		= "tot_loads",
	.cmp		= tot_loads_cmp,
	.entry		= tot_loads_entry,
	.id		= DIM_TOT_LOADS,
};

static struct c2c_dimension dim_percent_hitm = {
	HEADER("%hitm"),
	.name		= "percent_hitm",
	.cmp		= percent_hitm_cmp,
	.entry		= percent_hitm_entry,
	.id		= DIM_PERCENT_HITM,
};

static struct c2c_dimension dim_percent_ldmiss = {
	HEADER2(%All,Ld Miss),
	.name		= "percent_ldmiss",
	.cmp		= percent_ldmiss_cmp,
	.entry		= percent_ldmiss_entry,
	.id		= DIM_PERCENT_LDMISS,
};

static struct c2c_dimension dim_percent_rmt_hitm = {
	HEADER("%RmtHitm"),
	.name		= "percent_rmt_hitm",
	.cmp		= percent_rmt_hitm_cmp,
	.entry		= percent_rmt_hitm_entry,
	.id		= DIM_PERCENT_RMT_HITM,
};

static struct c2c_dimension dim_percent_lcl_hitm = {
	HEADER("%LclHitm"),
	.name		= "percent_lcl_hitm",
	.cmp		= percent_lcl_hitm_cmp,
	.entry		= percent_lcl_hitm_entry,
	.id		= DIM_PERCENT_LCL_HITM,
};

static struct c2c_dimension dim_percent_stores_l1hit = {
	HEADER("%StL1Hit"),
	.name		= "percent_stores_l1hit",
	.cmp		= percent_stores_l1hit_cmp,
	.entry		= percent_stores_l1hit_entry,
	.id		= DIM_PERCENT_STORES_L1HIT,
};

static struct c2c_dimension dim_percent_stores_l1miss = {
	HEADER("%StL1Miss"),
	.name		= "percent_stores_l1miss",
	.cmp		= percent_stores_l1miss_cmp,
	.entry		= percent_stores_l1miss_entry,
	.id		= DIM_PERCENT_STORES_L1MISS,
};

#undef HEADER
#undef HEADER2

static struct c2c_dimension *dimensions[] = {
	&dim_dcacheline,
	&dim_offset,
	&dim_daddr,
	&dim_iaddr,
	&dim_tot_hitm,
	&dim_lcl_hitm,
	&dim_rmt_hitm,
	&dim_stores,
	&dim_stores_l1hit,
	&dim_stores_l1miss,
	&dim_ld_fbhit,
	&dim_ld_l1hit,
	&dim_ld_l2hit,
	&dim_ld_llchit,
	&dim_ld_rmthit,
	&dim_ld_llcmiss,
	&dim_tot_recs,
	&dim_tot_loads,
	&dim_percent_hitm,
	&dim_percent_ldmiss,
	&dim_percent_rmt_hitm,
	&dim_percent_lcl_hitm,
	&dim_percent_stores_l1hit,
	&dim_percent_stores_l1miss,
	NULL,
};

static void set_dimension(struct c2c_dimension *dim)
{
	switch (dim->id) {
	case DIM_DCACHELINE:
		dim->width = 20;
		break;
	case DIM_OFFSET:
		dim->width = 5;
		break;
	case DIM_DADDR:
		dim->width = 20;
		break;
	case DIM_IADDR:
		dim->width = 20;
		break;
	case DIM_LD_FBHIT:
	case DIM_LD_L1HIT:
	case DIM_LD_L2HIT:
	case DIM_LD_LLC_MISS:
	case DIM_TOT_RECS:
	case DIM_TOT_LOADS:
	case DIM_PERCENT_HITM:
	case DIM_PERCENT_LDMISS:
		dim->width = 7;
		break;
	case DIM_TOT_HITM:
	case DIM_LCL_HITM:
	case DIM_RMT_HITM:
	case DIM_STORES:
	case DIM_STORES_L1HIT:
	case DIM_STORES_L1MISS:
	case DIM_LD_LLC_HIT:
	case DIM_LD_RMT_HIT:
	case DIM_PERCENT_LCL_HITM:
	case DIM_PERCENT_RMT_HITM:
	case DIM_PERCENT_STORES_L1HIT:
	case DIM_PERCENT_STORES_L1MISS:
		dim->width = 13;
		break;
	default:
		pr_err("internal dimension error\n");
		break;
	};
}

static void set_dimensions(void)
{
	unsigned int i;

	for (i = 0; dimensions[i]; i++)
		set_dimension(dimensions[i]);
}

static void fmt_free(struct perf_hpp_fmt *fmt)
{
	struct c2c_fmt *c2c_fmt;

	c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	free(c2c_fmt);
}

static bool fmt_equal(struct perf_hpp_fmt *a, struct perf_hpp_fmt *b)
{
	struct c2c_fmt *c2c_a = container_of(a, struct c2c_fmt, fmt);
	struct c2c_fmt *c2c_b = container_of(b, struct c2c_fmt, fmt);

	return c2c_a->dim == c2c_b->dim;
}

static struct c2c_dimension *get_dimension(const char *name)
{
	unsigned int i;

	for (i = 0; dimensions[i]; i++) {
		struct c2c_dimension *dim = dimensions[i];

		if (!strcmp(dim->name, name))
			return dim;
	};

	return NULL;
}

static struct perf_hpp_fmt *get_format(const char *name)
{
	struct c2c_dimension *dim = get_dimension(name);
	struct c2c_fmt *c2c_fmt;
	struct perf_hpp_fmt *fmt;

	if (!dim)
		return NULL;

	c2c_fmt = zalloc(sizeof(*c2c_fmt));
	if (!c2c_fmt)
		return NULL;

	c2c_fmt->dim = dim;

	fmt = &c2c_fmt->fmt;
	INIT_LIST_HEAD(&fmt->list);
	INIT_LIST_HEAD(&fmt->sort_list);

	memcpy(&c2c_fmt->fmt.phh, &dim->phh, sizeof(dim->phh));

	fmt->cmp	= dim->cmp;
	fmt->sort	= dim->cmp;
	fmt->entry	= dim->entry;
	fmt->header	= c2c_header;
	fmt->width	= c2c_width;
	fmt->collapse	= NULL;
	fmt->equal	= fmt_equal;
	fmt->free	= fmt_free;

	return fmt;
}

static int c2c_hists__init_output(struct c2c_hists *hists, char *name)
{
	struct perf_hpp_fmt *fmt = get_format(name);

	if (!fmt) {
		reset_dimensions();
		return output_field_add(&hists->list, name);
	}

	perf_hpp_list__column_register(&hists->list, fmt);
	return 0;
}

static int c2c_hists__init_sort(struct c2c_hists *hists, char *name)
{
	struct perf_hpp_fmt *fmt = get_format(name);

	if (!fmt) {
		reset_dimensions();
		return sort_dimension__add(&hists->list, name, NULL, 0);
	}

	perf_hpp_list__register_sort_field(&hists->list, fmt);
	return 0;
}

#define PARSE_LIST(_list, _fn)							\
	do {									\
		char *tmp, *tok;						\
		ret = 0;							\
										\
		if (!_list)							\
			break;							\
										\
		for (tok = strtok_r((char *)_list, ", ", &tmp);			\
				tok; tok = strtok_r(NULL, ", ", &tmp)) {	\
			ret = _fn(hists, tok);					\
			if (ret == -EINVAL) {					\
				error("Invalid --fields key: `%s'", tok);	\
				break;						\
			} else if (ret == -ESRCH) {				\
				error("Unknown --fields key: `%s'", tok);	\
				break;						\
			}							\
		}								\
	} while (0)

static int c2c_hists__init_hists(struct c2c_hists *hists)
{
	perf_hpp_list__init(&hists->list);
	return __hists__init(&hists->hists, &hists->list);
}

static int c2c_hists__init_list(struct c2c_hists *hists,
				const char *output_,
				const char *sort_)
{
	char *output = output_ ? strdup(output_) : NULL;
	char *sort   = sort_   ? strdup(sort_) : NULL;
	int ret;

	PARSE_LIST(output, c2c_hists__init_output);
	PARSE_LIST(sort,   c2c_hists__init_sort);

	/* copy sort keys to output fields */
	perf_hpp__setup_output_field(&hists->list);

	/*
	 * We dont need other sorting keys other than those
	 * we already specified. It also really slows down
	 * the processing a lot with big number of output
	 * fields, so switching this off for c2c.
	 */

#if 0
	/* and then copy output fields to sort keys */
	perf_hpp__append_sort_keys(&hists->list);
#endif

	free(output);
	free(sort);
	return ret;
}

static int c2c_hists__init(struct c2c_hists *hists,
			   const char *sort)
{
	c2c_hists__init_hists(hists);
	/*
	 * Initialize only with sort fields, we need to resort
	 * later anyway, and that's where we add output fields
	 * as well.
	 */
	return c2c_hists__init_list(hists, NULL, sort);
}

__maybe_unused
static int c2c_hists__reinit(struct c2c_hists *c2c_hists,
			     const char *output,
			     const char *sort)
{
	perf_hpp__reset_output_field(&c2c_hists->list);
	return c2c_hists__init_list(c2c_hists, output, sort);
}

static int resort_cl_cb(struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	struct c2c_hists *c2c_hists;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	c2c_hists = c2c_he->hists;

	if (HAS_HITMS(c2c_he)) {
		c2c_add_stats(&c2c.hitm_stats, &c2c_he->stats);
		c2c.shared_clines++;
	}

	if (c2c_hists) {
		hists__collapse_resort(&c2c_hists->hists, NULL);
		hists__output_resort(&c2c_hists->hists, NULL);
	}

	return 0;
}

static void print_c2c__display_stats(void)
{
	int llc_misses;
	struct c2c_stats *stats = &c2c.hists.stats;

	llc_misses = stats->t.lcl_dram +
		     stats->t.rmt_dram +
		     stats->t.rmt_hit +
		     stats->t.rmt_hitm;

	printf("=================================================\n");
	printf("            Trace Event Information              \n");
	printf("=================================================\n");
	printf("  Total records                     : %10d\n", stats->nr_entries);
	printf("  Locked Load/Store Operations      : %10d\n", stats->t.locks);
	printf("  Load Operations                   : %10d\n", stats->t.load);
	printf("  Loads - uncacheable               : %10d\n", stats->t.ld_uncache);
	printf("  Loads - IO                        : %10d\n", stats->t.ld_io);
	printf("  Loads - Miss                      : %10d\n", stats->t.ld_miss);
	printf("  Loads - no mapping                : %10d\n", stats->t.ld_noadrs);
	printf("  Load Fill Buffer Hit              : %10d\n", stats->t.ld_fbhit);
	printf("  Load L1D hit                      : %10d\n", stats->t.ld_l1hit);
	printf("  Load L2D hit                      : %10d\n", stats->t.ld_l2hit);
	printf("  Load LLC hit                      : %10d\n", stats->t.ld_llchit + stats->t.lcl_hitm);
	printf("  Load Local HITM                   : %10d\n", stats->t.lcl_hitm);
	printf("  Load Remote HITM                  : %10d\n", stats->t.rmt_hitm);
	printf("  Load Remote HIT                   : %10d\n", stats->t.rmt_hit);
	printf("  Load Local DRAM                   : %10d\n", stats->t.lcl_dram);
	printf("  Load Remote DRAM                  : %10d\n", stats->t.rmt_dram);
	printf("  Load MESI State Exclusive         : %10d\n", stats->t.ld_excl);
	printf("  Load MESI State Shared            : %10d\n", stats->t.ld_shared);
	printf("  Load LLC Misses                   : %10d\n", llc_misses);
	printf("  LLC Misses to Local DRAM          : %10.1f%%\n", ((double)stats->t.lcl_dram/(double)llc_misses) * 100.);
	printf("  LLC Misses to Remote DRAM         : %10.1f%%\n", ((double)stats->t.rmt_dram/(double)llc_misses) * 100.);
	printf("  LLC Misses to Remote cache (HIT)  : %10.1f%%\n", ((double)stats->t.rmt_hit /(double)llc_misses) * 100.);
	printf("  LLC Misses to Remote cache (HITM) : %10.1f%%\n", ((double)stats->t.rmt_hitm/(double)llc_misses) * 100.);
	printf("  Store Operations                  : %10d\n", stats->t.store);
	printf("  Store - uncacheable               : %10d\n", stats->t.st_uncache);
	printf("  Store - no mapping                : %10d\n", stats->t.st_noadrs);
	printf("  Store L1D Hit                     : %10d\n", stats->t.st_l1hit);
	printf("  Store L1D Miss                    : %10d\n", stats->t.st_l1miss);
	printf("  No Page Map Rejects               : %10d\n", stats->t.nomap);
	printf("  Unable to parse data source       : %10d\n", stats->t.noparse);
}

static void print_shared_cacheline_info(void)
{
	struct c2c_stats *stats = &c2c.hitm_stats;
	int hitm_cnt = stats->t.lcl_hitm + stats->t.rmt_hitm;

	printf("=================================================\n");
	printf("    Global Shared Cache Line Event Information   \n");
	printf("=================================================\n");
	printf("  Total Shared Cache Lines          : %10d\n", c2c.shared_clines);
	printf("  Load HITs on shared lines         : %10d\n", stats->t.load);
	printf("  Fill Buffer Hits on shared lines  : %10d\n", stats->t.ld_fbhit);
	printf("  L1D hits on shared lines          : %10d\n", stats->t.ld_l1hit);
	printf("  L2D hits on shared lines          : %10d\n", stats->t.ld_l2hit);
	printf("  LLC hits on shared lines          : %10d\n", stats->t.ld_llchit + stats->t.lcl_hitm);
	printf("  Locked Access on shared lines     : %10d\n", stats->t.locks);
	printf("  Store HITs on shared lines        : %10d\n", stats->t.store);
	printf("  Store L1D hits on shared lines    : %10d\n", stats->t.st_l1hit);
	printf("  Total Merged records              : %10d\n", hitm_cnt + stats->t.store);
}

static void perf_c2c__hists_fprintf(FILE *out)
{
	struct rb_node *nd;

	print_c2c__display_stats();
	fprintf(out, "\n");
	print_shared_cacheline_info();

	if (c2c.stats_only)
		return;

	fprintf(out, "\nShared Cache Line Distribution Pareto\n\n");
	hists__fprintf(&c2c.hists.hists, true, 0, 0, 0, stdout);

	fprintf(out, "\nShared Data Cache Line Table\n\n");

	nd = rb_first(&c2c.hists.hists.entries);

	do {
		struct c2c_hist_entry *c2c_he;
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);

		c2c_he = container_of(he, struct c2c_hist_entry, he);

		fprintf(out, "\nCacheline: \n\n");

		hists__fprintf(&c2c_he->hists->hists, true, 0, 0, 0, stdout);

		nd = rb_next(nd);
	} while (nd);
}

static void c2c_browser__update_nr_entries(struct hist_browser *hb)
{
	u64 nr_entries = 0;
	struct rb_node *nd = rb_first(&hb->hists->entries);

	do {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);

		if (!he->filtered)
			nr_entries++;

		nd = rb_next(nd);
	} while (nd);

	hb->nr_non_filtered_entries = nr_entries;
}

struct c2c_cacheline_browser {
	struct hist_browser	 hb;
	struct hist_entry	*he;
};

static int
perf_c2c_cacheline_browser__title(struct hist_browser *browser,
				  char *bf, size_t size)
{
	struct c2c_cacheline_browser *cl_browser;
	struct hist_entry *he;
	uint64_t addr = 0;

	cl_browser = container_of(browser, struct c2c_cacheline_browser, hb);
	he = cl_browser->he;

        if (he->mem_info)
                addr = cl_address(he->mem_info->daddr.addr);

	scnprintf(bf, size, "Cacheline 0x%lx", addr);
	return 0;
}

static struct c2c_cacheline_browser*
c2c_cacheline_browser__new(struct hists *hists, struct hist_entry *he)
{
	struct c2c_cacheline_browser *browser;

	browser = zalloc(sizeof(*browser));
	if (browser) {
		hist_browser__init(&browser->hb, hists);
		browser->hb.title = perf_c2c_cacheline_browser__title;
		browser->he	  = he;
	}

	return browser;
}

static int perf_c2c__browse_cacheline(struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	struct c2c_hists *c2c_hists;
	struct c2c_cacheline_browser *cl_browser;
	struct hist_browser *browser;
	int key = -1;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	c2c_hists = c2c_he->hists;

	cl_browser = c2c_cacheline_browser__new(&c2c_hists->hists, he);
	if (cl_browser == NULL)
		return -1;

	browser = &cl_browser->hb;

	/* reset abort key so that it can get Ctrl-C as a key */
	SLang_reset_tty();
	SLang_init_tty(0, 0, 0);

	c2c_browser__update_nr_entries(browser);

	while (1) {
		key = hist_browser__run(browser, "help");

		switch (key) {
		case 'q':
			goto out;
		default:
			break;
		}
	}

out:
	free(cl_browser);
	return 0;
}

static int perf_c2c_browser__title(struct hist_browser *browser,
				   char *bf, size_t size)
{
	scnprintf(bf, size,
		  "%lu entries", browser->nr_non_filtered_entries);
	return 0;
}

static struct hist_browser*
perf_c2c_browser__new(struct hists *hists)
{
	struct hist_browser *browser = hist_browser__new(hists);

	if (browser) {
		browser->title = perf_c2c_browser__title;
		browser->c2c_filter = true;
	}

	return browser;
}

static int perf_c2c__hists_browse(struct hists *hists)
{
	struct hist_browser *browser;
	int key = -1;

	browser = perf_c2c_browser__new(hists);
	if (browser == NULL)
		return -1;

	/* reset abort key so that it can get Ctrl-C as a key */
	SLang_reset_tty();
	SLang_init_tty(0, 0, 0);

	c2c_browser__update_nr_entries(browser);

	while (1) {
		key = hist_browser__run(browser, "help");

		switch (key) {
		case 'q':
			goto out;
		case 'd':
			perf_c2c__browse_cacheline(browser->he_selection);
			break;
		default:
			break;
		}
	}

out:
	hist_browser__delete(browser);
	return 0;
}

static int perf_c2c__report(int argc, const char **argv)
{
	struct perf_session *session;
	struct ui_progress prog;
	struct perf_data_file file = {
		.mode = PERF_DATA_MODE_READ,
	};
	bool use_stdio = false;
	const struct option c2c_options[] = {
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_INCR('v', "verbose", &verbose,
		 "be more verbose (show counter open errors, etc)"),
	OPT_STRING('i', "input", &input_name, "file",
		   "the input file to process"),
	OPT_BOOLEAN(0, "stdio", &use_stdio,
		    "Use the stdio interface"),
	OPT_BOOLEAN(0, "stats", &c2c.stats_only,
		    "Use the stdio interface"),
	OPT_END()
	};
	int err = 0;

	argc = parse_options(argc, argv, c2c_options, report_c2c_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc)
		usage_with_options(report_c2c_usage, c2c_options);

	if (c2c.stats_only)
		use_stdio = true;

	if (use_stdio)
		use_browser = 0;
	else
		use_browser = 1;

	setup_browser(false);

	if (!input_name || !strlen(input_name))
		input_name = "perf.data";

	file.path = input_name;

	set_dimensions();

	err = c2c_hists__init(&c2c.hists, "dcacheline");
	if (err) {
		pr_debug("Failed to initialize hists\n");
		goto out;
	}

	session = perf_session__new(&file, 0, &c2c.tool);
	if (session == NULL) {
		pr_debug("No memory for session\n");
		goto out;
	}

	if (symbol__init(&session->header.env) < 0) {
		goto out_session;
	}

	/* No pipe support at the moment. */
	if (perf_data_file__is_pipe(session->file)) {
		pr_debug("No pipe support at the moment.\n");
		goto out_session;
	}

	err = perf_session__process_events(session);
	if (err) {
		pr_err("failed to process sample\n");
		goto out_session;
	}

	ui_progress__init(&prog, c2c.hists.hists.nr_entries, "Sorting...");

	hists__collapse_resort(&c2c.hists.hists, NULL);
	hists__output_resort_cb(&c2c.hists.hists, &prog, resort_cl_cb);

	ui_progress__finish();

	if (use_stdio)
		perf_c2c__hists_fprintf(stdout);
	else
		perf_c2c__hists_browse(&c2c.hists.hists);

out_session:
	perf_session__delete(session);
out:
	return err;
}

static int parse_record_events(const struct option *opt __maybe_unused,
			       const char *str, int unset __maybe_unused)
{
	bool *event_set = (bool*) opt->value;
	int j;

	*event_set = true;

	if (strcmp(str, "list"))
		return perf_mem_events__parse(str);

	fprintf(stderr, "available events:\n");

	for (j = 0; j < PERF_MEM_EVENTS__MAX; j++) {
		struct perf_mem_event *e = &perf_mem_events[j];

		fprintf(stderr, "%s %s\n",
			e->supported ? "[ok] " : "[n/a]", e->name);
	}

	exit(0);
}


static const char * const __usage_record[] = {
	"perf c2c record [<options>] [<command>]",
	"perf c2c record [<options>] -- <command> [<options>]",
	NULL
};

static const char * const *record_mem_usage = __usage_record;

static int perf_c2c__record(int argc, const char **argv)
{
	int rec_argc, i = 0, j;
	const char **rec_argv;
	int ret;
	bool all_user = false, all_kernel = false;
	bool event_set = false;
	struct option options[] = {
	OPT_CALLBACK('e', "event", &event_set, "event",
		     "event selector. use 'perf mem record -e list' to list available events",
		     parse_record_events),
	OPT_INCR('v', "verbose", &verbose,
		 "be more verbose (show counter open errors, etc)"),
	OPT_BOOLEAN('u', "--all-user", &all_user, "collect only user level data"),
	OPT_BOOLEAN('k', "--all-kernel", &all_kernel, "collect only kernel level data"),
	OPT_UINTEGER('l', "ldlat", &perf_mem_events__loads_ldlat, "mem-loads latency"),
	OPT_END()
	};

	if (perf_mem_events__init()) {
		pr_err("failed: memory events not supported\n");
		return -1;
	}

	argc = parse_options(argc, argv, options, record_mem_usage,
			     PARSE_OPT_KEEP_UNKNOWN);

	rec_argc = argc + 9; /* max number of arguments */
	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	if (!rec_argv)
		return -1;

	rec_argv[i++] = "record";

	if (!event_set) {
		perf_mem_events[PERF_MEM_EVENTS__LOAD].record  = true;
		perf_mem_events[PERF_MEM_EVENTS__STORE].record = true;
	}

	if (perf_mem_events[PERF_MEM_EVENTS__LOAD].record)
		rec_argv[i++] = "-W";

	rec_argv[i++] = "-d";

	for (j = 0; j < PERF_MEM_EVENTS__MAX; j++) {
		if (!perf_mem_events[j].record)
			continue;

		if (!perf_mem_events[j].supported) {
			pr_err("failed: event '%s' not supported\n",
			       perf_mem_events[j].name);
			return -1;
		}

		rec_argv[i++] = "-e";
		rec_argv[i++] = perf_mem_events__name(j);
	};

	if (all_user)
		rec_argv[i++] = "--all-user";

	if (all_kernel)
		rec_argv[i++] = "--all-kernel";

	for (j = 0; j < argc; j++, i++)
		rec_argv[i] = argv[j];

	if (verbose > 0) {
		pr_debug("calling: record ");

		j = 0;

		while (rec_argv[j]) {
			pr_debug("%s ", rec_argv[j]);
			j++;
		}
		pr_debug("\n");
	}

	ret = cmd_record(i, rec_argv, NULL);
	free(rec_argv);
	return ret;
}

int cmd_c2c(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const struct option c2c_options[] = {
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_END()
	};

	argc = parse_options(argc, argv, c2c_options, c2c_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (!argc)
		usage_with_options(c2c_usage, c2c_options);

	if (!strncmp(argv[0], "rec", 3)) {
		return perf_c2c__record(argc, argv);
	} else if (!strncmp(argv[0], "rep", 3)) {
		return perf_c2c__report(argc, argv);
	} else {
		usage_with_options(c2c_usage, c2c_options);
	}

	return 0;
}
