#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/stringify.h>
#include <asm/bug.h>
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
#include <asm/bug.h>
#include "ui/browsers/hists.h"
#include "evlist.h"

struct c2c_hists {
	struct hists		hists;
	struct perf_hpp_list	list;
	struct c2c_stats	stats;
};

struct c2c_hist_entry {
	struct c2c_hists	*hists;
	struct c2c_stats	stats;
	unsigned long		*cpuset;
	struct c2c_stats	*node_stats;
	/*
	 * must be at the end,
	 * because of its callchain dynamic entry
	 */
	struct hist_entry	he;
};

struct perf_c2c {
	struct perf_tool	tool;
	struct c2c_hists	hists;

	unsigned long		**nodes;
	int			 nodes_cnt;
	int			 cpus_cnt;
	int			*cpu2node;
	int			 node_info;

	bool			 use_stdio;
	bool			 stats_only;

	/* HITM shared clines stats */
	struct c2c_stats	hitm_stats;
	int			shared_clines;

	int			 display;
};

static struct perf_c2c c2c;

static int setup_nodes(struct perf_session *session)
{
	struct numa_node *n;
	unsigned long **nodes;
	int node, cpu;
	int *cpu2node;

	c2c.nodes_cnt = session->header.env.nr_numa_nodes;
	c2c.cpus_cnt  = session->header.env.nr_cpus_online;

	n = session->header.env.numa_nodes;
	if (!n)
		return -EINVAL;

	nodes = zalloc(sizeof(unsigned long*) * c2c.nodes_cnt);
	if (!nodes)
		return -ENOMEM;

	c2c.nodes = nodes;

	cpu2node = zalloc(sizeof(int) * c2c.cpus_cnt);
	if (!cpu2node)
		return -ENOMEM;

	for (cpu = 0; cpu < c2c.cpus_cnt; cpu++)
		cpu2node[cpu] = -1;

	c2c.cpu2node = cpu2node;

	/* TODO - alloc cpu_set_t and check boundaries */
	for (node = 0; node < c2c.nodes_cnt; node++) {
		struct cpu_map *map = n[node].map;
		unsigned long *set;

		set = bitmap_alloc(c2c.cpus_cnt);
		if (!set)
			return -ENOMEM;

		bitmap_zero(set, c2c.cpus_cnt);

		for (cpu = 0; cpu < map->nr; cpu++) {
			set_bit(map->map[cpu], set);

			if (WARN_ONCE(cpu2node[map->map[cpu]] != -1, "node/cpu topology bug"))
				return -EINVAL;

			cpu2node[map->map[cpu]] = node;
		}

		nodes[node] = set;
	}

	return 0;
}

static void* c2c_he_zalloc(size_t size)
{
	struct c2c_hist_entry *c2c_he;

	c2c_he = zalloc(size + sizeof(*c2c_he));
	if (!c2c_he)
		return NULL;

	c2c_he->cpuset = bitmap_alloc(c2c.cpus_cnt);
	if (!c2c_he->cpuset)
		return NULL;

	bitmap_zero(c2c_he->cpuset, c2c.cpus_cnt);

	c2c_he->node_stats = zalloc(c2c.nodes_cnt * sizeof(*c2c_he->node_stats));
	if (!c2c_he->node_stats)
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

	free(c2c_he->cpuset);
	free(c2c_he->node_stats);
	free(c2c_he);
}

static struct hist_entry_ops c2c_entry_ops = {
	.new	= c2c_he_zalloc,
	.free	= c2c_he_free,
};

static int c2c_hists__init(struct c2c_hists *hists,
			   const char *sort,
			   int nr_header_lines);

static struct c2c_hists*
he__get_c2c_hists(struct hist_entry *he,
		  const char *sort,
		  int nr_header_lines)
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

	ret = c2c_hists__init(hists, sort, nr_header_lines);
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
	struct c2c_hists *c2c_hists = &c2c.hists;
	struct c2c_hist_entry *c2c_he;
	struct c2c_stats stats = { 0 };
	struct hist_entry *he;
	struct addr_location al;
	struct mem_info *mi, *mi_dup;
	int ret;

	if (machine__resolve(machine, &al, sample) < 0) {
		pr_debug("problem processing %d event, skipping it.\n",
			 event->header.type);
		return -1;
	}

	ret = sample__resolve_callchain(sample, &callchain_cursor, NULL,
                                        evsel, &al, sysctl_perf_event_max_stack);
	if (ret)
		goto out;

	mi = sample__resolve_mem(sample, &al);
	if (mi == NULL)
		return -ENOMEM;

	mi_dup = memdup(mi, sizeof(*mi));
	if (!mi_dup)
		goto free_mi;

	c2c_decode_stats(&stats, mi, sample->weight);

	he = hists__add_entry_ops(&c2c_hists->hists, &c2c_entry_ops,
				  &al, NULL, NULL, mi,
				  sample, true);
	if (he == NULL)
		goto free_mi_dup;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	c2c_add_stats(&c2c_he->stats, &stats);
	c2c_add_stats(&c2c_hists->stats, &stats);

	set_bit(sample->cpu, c2c_he->cpuset);

	hists__inc_nr_samples(&c2c_hists->hists, he->filtered);
	ret = hist_entry__append_callchain(he, sample);

	if (!ret) {
		int node = c2c.cpu2node[sample->cpu];

		mi = mi_dup;

		mi_dup = memdup(mi, sizeof(*mi));
		if (!mi_dup)
			goto free_mi;

		c2c_hists = he__get_c2c_hists(he, "offset", 2);
		if (!c2c_hists)
			goto free_mi_dup;

		he = hists__add_entry_ops(&c2c_hists->hists, &c2c_entry_ops,
					  &al, NULL, NULL, mi,
					  sample, true);
		if (he == NULL)
			goto free_mi_dup;

		c2c_he = container_of(he, struct c2c_hist_entry, he);
		c2c_add_stats(&c2c_he->stats, &stats);
		c2c_add_stats(&c2c_hists->stats, &stats);
		c2c_add_stats(&c2c_he->node_stats[node], &stats);

		set_bit(sample->cpu, c2c_he->cpuset);

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

struct c2c_header {
	const char *text;
	int	    span;
};

#define C2C_HEADER_MAX 3

struct c2c_dimension {
	struct c2c_header header[C2C_HEADER_MAX];
	const char *name;
	int64_t (*cmp)(struct perf_hpp_fmt *fmt,
		       struct hist_entry *, struct hist_entry *);
	int (*entry)(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		     struct hist_entry *he);
	int (*color)(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		     struct hist_entry *he);
	int width;
	struct sort_entry *se;
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
	struct c2c_dimension *dim;

	c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	dim = c2c_fmt->dim;

	return dim->se ? hists__col_len(hists, dim->se->se_width_idx) :
			 c2c_fmt->dim->width;
}

static int c2c_header(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		      struct hists *hists, int line, int *span)
{
	struct perf_hpp_list *hpp_list = hists->hpp_list;
	struct c2c_fmt *c2c_fmt;
	struct c2c_dimension *dim;
	const char *text = NULL;
	int width = c2c_width(fmt, hpp, hists);

	c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	dim = c2c_fmt->dim;

	if (dim->se) {
		if (line == hpp_list->nr_header_lines - 1) {
			text = dim->header[line].text;
			if (!text)
				text = dim->se->se_header;
		}
	} else {
		text = dim->header[line].text;

		if (*span) {
			(*span)--;
			return 0;
		} else {
			*span = dim->header[line].span;
		}
	}

	if (text == NULL)
		text = "";

	return scnprintf(hpp->buf, hpp->size, "%*s", width, text);
}

static char* hex_str(u64 val)
{
	static char buf[20];

	snprintf(buf, 20, "0x%" PRIx64, val);
	return buf;
}

static int64_t
dcacheline_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	       struct hist_entry *left, struct hist_entry *right)
{
	u64 l, r;

	if (!left->mem_info)  return -1;
	if (!right->mem_info) return 1;

	/* al_addr does all the right addr - start + offset calculations */
	l = cl_address(left->mem_info->daddr.al_addr);
	r = cl_address(right->mem_info->daddr.al_addr);

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
		addr = cl_address(he->mem_info->daddr.al_addr);

	return snprintf(hpp->buf, hpp->size, "%*s", width, hex_str(addr));
}

static int offset_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			struct hist_entry *he)
{
	uint64_t addr = 0;
	int width = c2c_width(fmt, hpp, he->hists);

	if (he->mem_info)
		addr = cl_offset(he->mem_info->daddr.al_addr);

	return snprintf(hpp->buf, hpp->size, "%*s", width, hex_str(addr));
}

static int64_t
offset_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	   struct hist_entry *left, struct hist_entry *right)
{
	uint64_t l = 0, r = 0;

	if (left->mem_info)
		l = cl_offset(left->mem_info->daddr.addr);
	if (right->mem_info)
		r = cl_offset(right->mem_info->daddr.addr);

	return (int64_t)(r - l);
}

static int
iaddr_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	    struct hist_entry *he)
{
	uint64_t addr = 0;
	int width = c2c_width(fmt, hpp, he->hists);

	if (he->mem_info)
		addr = he->mem_info->iaddr.al_addr;

	return snprintf(hpp->buf, hpp->size, "%*" PRIx64, width, addr);
}

static int64_t
iaddr_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	  struct hist_entry *left, struct hist_entry *right)
{
	return sort__iaddr_cmp(left, right);
}

static int
tot_hitm_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	       struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	unsigned int tot_hitm;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	tot_hitm = c2c_he->stats.lcl_hitm + c2c_he->stats.rmt_hitm;

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
			c2c_he->stats.lcl_hitm);
}

static int
rmt_hitm_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	       struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width,
			c2c_he->stats.rmt_hitm);
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

	tot_hitm_left  = c2c_left->stats.lcl_hitm + c2c_left->stats.rmt_hitm;
	tot_hitm_right = c2c_right->stats.lcl_hitm + c2c_right->stats.rmt_hitm;
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

	return c2c_left->stats.lcl_hitm - c2c_right->stats.lcl_hitm;
}

static int64_t
rmt_hitm_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.rmt_hitm - c2c_right->stats.rmt_hitm;
}

static int
stores_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	     struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width, c2c_he->stats.store);
}

static int
stores_l1hit_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		   struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width, c2c_he->stats.st_l1hit);
}

static int
stores_l1miss_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		    struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width, c2c_he->stats.st_l1miss);
}

static int64_t
stores_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	   struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.store - c2c_right->stats.store;
}

static int64_t
stores_l1hit_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
		 struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.st_l1hit - c2c_right->stats.st_l1hit;
}

static int64_t
stores_l1miss_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
		  struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.st_l1miss - c2c_right->stats.st_l1miss;
}

static int
ld_fbhit_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	       struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width, c2c_he->stats.ld_fbhit);
}

static int
ld_l1hit_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	       struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width, c2c_he->stats.ld_l1hit);
}

static int
ld_l2hit_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	       struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width, c2c_he->stats.ld_l2hit);
}

static int64_t
ld_fbhit_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.ld_fbhit - c2c_right->stats.ld_fbhit;
}

static int64_t
ld_l1hit_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.ld_l1hit - c2c_right->stats.ld_l1hit;
}

static int64_t
ld_l2hit_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.ld_l2hit - c2c_right->stats.ld_l2hit;
}

static int
ld_llchit_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width, c2c_he->stats.ld_llchit);
}

static int
ld_rmthit_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	return snprintf(hpp->buf, hpp->size, "%*u", width, c2c_he->stats.rmt_hit);
}

static int64_t
ld_llchit_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.ld_llchit - c2c_right->stats.ld_llchit;
}

static int64_t
ld_rmthit_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.rmt_hit - c2c_right->stats.rmt_hit;
}

static uint64_t llc_miss(struct c2c_stats *stats)
{
	uint64_t llcmiss;

	llcmiss = stats->lcl_dram +
		  stats->rmt_dram +
		  stats->rmt_hitm +
		  stats->rmt_hit;

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

	lclmiss  = stats->lcl_dram +
		   stats->rmt_dram +
		   stats->rmt_hitm +
		   stats->rmt_hit;

	ldcnt    = lclmiss +
		   stats->ld_fbhit +
		   stats->ld_l1hit +
		   stats->ld_l2hit +
		   stats->ld_llchit +
		   stats->lcl_hitm;

	total    = ldcnt +
		   stats->st_l1hit +
		   stats->st_l1miss;

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

	lclmiss  = stats->lcl_dram +
		   stats->rmt_dram +
		   stats->rmt_hitm +
		   stats->rmt_hit;

	ldcnt    = lclmiss +
		   stats->ld_fbhit +
		   stats->ld_l1hit +
		   stats->ld_l2hit +
		   stats->ld_llchit +
		   stats->lcl_hitm;

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

typedef double (get_percent_cb)(struct c2c_hist_entry*);

static int
percent_color(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	      struct hist_entry *he, get_percent_cb get_percent)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	double per;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	per = get_percent(c2c_he);

	if (use_browser)
		return __hpp__slsmg_color_printf(hpp, "%*.2f%%", width - 1, per);
	else
		return hpp_color_scnprintf(hpp, "%*.2f%%", width - 1, per);
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

	st  = stats->rmt_hitm;
	tot = total->rmt_hitm;

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

static int
percent_hitm_color(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		   struct hist_entry *he)
{
	return percent_color(fmt, hpp, he, percent_hitm);
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

	st  = stats->rmt_hitm;
        tot = total->lcl_dram +
              total->rmt_dram +
              total->rmt_hit +
              total->rmt_hitm;

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
	per = percent_ldmiss(c2c_he);

	snprintf(buf, 10, "%.2F%%", per);
	return snprintf(hpp->buf, hpp->size, "%*s", width, buf);
}

static int
percent_ldmiss_color(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		     struct hist_entry *he)
{
	return percent_color(fmt, hpp, he, percent_ldmiss);
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

#define PERCENT(__h, __f) percent(he_stats(__h)->__f, total_stats(__h)->__f)

#define PERCENT_FN(__f)								\
static double percent_ ## __f(struct c2c_hist_entry *c2c_he)			\
{										\
	struct c2c_hists *hists;						\
										\
	hists = container_of(c2c_he->he.hists, struct c2c_hists, hists);	\
	return percent(c2c_he->stats.__f, hists->stats.__f);			\
}

PERCENT_FN(rmt_hitm)
PERCENT_FN(lcl_hitm)
PERCENT_FN(st_l1hit)
PERCENT_FN(st_l1miss)

static int
percent_rmt_hitm_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);
	double per = PERCENT(he, rmt_hitm);
	char buf[10];

	snprintf(buf, 10, "%.2F%%", per);
	return snprintf(hpp->buf, hpp->size, "%*s", width, buf);
}

static int
percent_rmt_hitm_color(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he)
{
	return percent_color(fmt, hpp, he, percent_rmt_hitm);
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
	char buf[10];

	snprintf(buf, 10, "%.2F%%", per);
	return snprintf(hpp->buf, hpp->size, "%*s", width, buf);
}

static int
percent_lcl_hitm_color(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		       struct hist_entry *he)
{
	return percent_color(fmt, hpp, he, percent_lcl_hitm);
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
	char buf[10];

	snprintf(buf, 10, "%.2F%%", per);
	return snprintf(hpp->buf, hpp->size, "%*s", width, buf);
}

static int
percent_stores_l1hit_color(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			   struct hist_entry *he)
{
	return percent_color(fmt, hpp, he, percent_st_l1hit);
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
	char buf[10];

	snprintf(buf, 10, "%.2F%%", per);
	return snprintf(hpp->buf, hpp->size, "%*s", width, buf);
}

static int
percent_stores_l1miss_color(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			    struct hist_entry *he)
{
	return percent_color(fmt, hpp, he, percent_st_l1miss);
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

static int
dram_lcl_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	       struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);
	struct c2c_hist_entry *c2c_he;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	return snprintf(hpp->buf, hpp->size, "%*d", width, c2c_he->stats.lcl_dram);
}

static int64_t
dram_lcl_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.lcl_dram - c2c_right->stats.lcl_dram;
}

static int
dram_rmt_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	       struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);
	struct c2c_hist_entry *c2c_he;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	return snprintf(hpp->buf, hpp->size, "%*d", width, c2c_he->stats.rmt_dram);
}

static int64_t
dram_rmt_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	     struct hist_entry *left, struct hist_entry *right)
{
	struct c2c_hist_entry *c2c_left;
	struct c2c_hist_entry *c2c_right;

	c2c_left  = container_of(left, struct c2c_hist_entry, he);
	c2c_right = container_of(right, struct c2c_hist_entry, he);

	return c2c_left->stats.rmt_dram - c2c_right->stats.rmt_dram;
}

static int
pid_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	  struct hist_entry *he)
{
	int width = c2c_width(fmt, hpp, he->hists);

	return snprintf(hpp->buf, hpp->size, "%*d", width, he->thread->pid_);
}

static int64_t
pid_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	struct hist_entry *left, struct hist_entry *right)
{
	return left->thread->pid_ - right->thread->pid_;
}

static int64_t
empty_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
	  struct hist_entry *left __maybe_unused,
	  struct hist_entry *right __maybe_unused)
{
	return 0;
}

static int
node_entry(struct perf_hpp_fmt *fmt __maybe_unused, struct perf_hpp *hpp,
	   struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	bool first = true;
	int node;
	int ret = 0;

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	for (node = 0; node < c2c.nodes_cnt; node++) {
		DECLARE_BITMAP(set, c2c.cpus_cnt);

		bitmap_zero(set, c2c.cpus_cnt);
		bitmap_and(set, c2c_he->cpuset, c2c.nodes[node], c2c.cpus_cnt);

		if (!bitmap_weight(set, c2c.cpus_cnt))
			continue;

		if (!first) {
			ret = scnprintf(hpp->buf, hpp->size, " ");
			advance_hpp(hpp, ret);
		}

		switch (c2c.node_info) {
		case 0:
			ret = scnprintf(hpp->buf, hpp->size, "%d", node);
			advance_hpp(hpp, ret);
			break;
		case 1:
		{
			struct c2c_stats *stats = &c2c_he->node_stats[node];
			int num = bitmap_weight(c2c_he->cpuset, c2c.cpus_cnt);

			ret = scnprintf(hpp->buf, hpp->size, "%2d{%2d ", node, num);
			advance_hpp(hpp, ret);


			if (c2c_he->stats.rmt_hitm > 0)
				ret = scnprintf(hpp->buf, hpp->size, "%5.1f%% ", 100. * ((double)stats->rmt_hitm / (double) c2c_he->stats.rmt_hitm));
			else
				ret = scnprintf(hpp->buf, hpp->size, "%6s ", "n/a");

			advance_hpp(hpp, ret);

			if (c2c_he->stats.store > 0)
				ret = scnprintf(hpp->buf, hpp->size, "%5.1f%%}", 100. * ((double)stats->store / (double)c2c_he->stats.store));
			else
				ret = scnprintf(hpp->buf, hpp->size, "%6s}", "n/a");

			advance_hpp(hpp, ret);
			break;
		}
		case 2:
			ret = scnprintf(hpp->buf, hpp->size, "%d{", node);
			advance_hpp(hpp, ret);

			ret = bitmap_snprintf(set, c2c.cpus_cnt, hpp->buf, hpp->size);
			advance_hpp(hpp, ret);

			ret = scnprintf(hpp->buf, hpp->size, "}");
			advance_hpp(hpp, ret);
			break;
		default:
			break;
		}

		first = false;
	}

	return 0;
}

static int
mean_entry(struct perf_hpp_fmt *fmt __maybe_unused, struct perf_hpp *hpp,
	   struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	char buf[10];
	double mean;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	mean = avg_stats(&c2c_he->stats.stats);
	snprintf(buf, 10, "%6.0f", mean);

	return snprintf(hpp->buf, hpp->size, "%*s", width, buf);
}

static int
median_entry(struct perf_hpp_fmt *fmt __maybe_unused, struct perf_hpp *hpp,
	   struct hist_entry *he __maybe_unused)
{
	int width = c2c_width(fmt, hpp, he->hists);
	char buf[10];

	snprintf(buf, 10, "%6d", 0);
	return snprintf(hpp->buf, hpp->size, "%*s", width, buf);
}

static int
stddev_entry(struct perf_hpp_fmt *fmt __maybe_unused, struct perf_hpp *hpp,
	   struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	double std;
	char buf[10];

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	std = stddev_stats(&c2c_he->stats.stats);

	snprintf(buf, 10, "%5.1f", std);
	return snprintf(hpp->buf, hpp->size, "%*s", width, buf);
}

static int
cpucnt_entry(struct perf_hpp_fmt *fmt __maybe_unused, struct perf_hpp *hpp,
	     struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	int width = c2c_width(fmt, hpp, he->hists);
	char buf[10];

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	snprintf(buf, 10, "%d", bitmap_weight(c2c_he->cpuset, c2c.cpus_cnt));
	return snprintf(hpp->buf, hpp->size, "%*s", width, buf);
}

/* HEADER_* macros are for main browser */

#define HEADER_0(__h)	\
	.header[1] = {		\
		.text = __h,	\
	}

#define HEADER_1(__h0, __h1)		\
	.header[0] = {	\
		.text = __h0,		\
	},				\
	.header[1] = {	\
		.text = __h1,		\
	}

#define HEADER_SPAN(__h0, __h1, __s)	\
	.header[0] = {			\
		.text = __h0,		\
		.span = __s,		\
	},				\
	.header[1] = {			\
		.text = __h1,		\
	}

#define HEADER_SPAN_1(__h1)		\
	.header[1] = {			\
		.text = __h1,		\
	}

/* HEADER_CL_* macros are for cacheline browser */

#define HEADER_CL_0(__h)	\
	.header[1] = {		\
		.text = __h,	\
	}

#define HEADER_CL_1(__h0, __h1)	\
	.header[0] = {		\
		.text = __h0,	\
	},			\
	.header[1] = {		\
		.text = __h1,	\
	}

#define HEADER_CL_SPAN(__h0, __h1, __s)	\
	.header[0] = {			\
		.text = __h0,		\
		.span = __s,		\
	},				\
	.header[1] = {			\
		.text = __h1,		\
	}

#define HEADER_CL_SPAN_1(__h1)		\
	.header[1] = {			\
		.text = __h1,		\
	}

/* HEADER_OFF_* macros are for cacheline browser */

#define HEADER_OFF_0(__h)	\
	.header[1] = {		\
		.text = __h,	\
	}

#define HEADER_OFF_SPAN(__h0, __h1, __s)	\
	.header[0] = {			\
		.text = __h0,		\
		.span = __s,		\
	},				\
	.header[1] = {			\
		.text = __h1,		\
	}

#define HEADER_OFF_SPAN_1(__h1)		\
	.header[1] = {			\
		.text = __h1,		\
	}

static struct c2c_dimension dim_dcacheline = {
	HEADER_0("Cacheline"),
	.name		= "dcacheline",
	.cmp		= dcacheline_cmp,
	.entry		= dcacheline_entry,
	.width		= 20,
};

static struct c2c_dimension dim_offset = {
	HEADER_CL_0("Off"),
	.name		= "offset",
	.cmp		= offset_cmp,
	.entry		= offset_entry,
	.width		= 20,
};

static struct c2c_dimension dim_iaddr = {
	HEADER_OFF_0("Code address"),
	.name		= "iaddr",
	.cmp		= iaddr_cmp,
	.entry		= iaddr_entry,
	.width		= 20,
};

static struct c2c_dimension dim_dsymbol = {
	.name		= "dsymbol",
	.se		= &sort_mem_daddr_sym,
};

static struct c2c_dimension dim_tot_hitm = {
	HEADER_SPAN("----- LLC Load Hitm -----", "Total", 2),
	.name		= "tot_hitm",
	.cmp		= tot_hitm_cmp,
	.entry		= tot_hitm_entry,
	.width		= 7,
};

static struct c2c_dimension dim_lcl_hitm = {
	HEADER_SPAN_1("Lcl"),
	.name		= "lcl_hitm",
	.cmp		= lcl_hitm_cmp,
	.entry		= lcl_hitm_entry,
	.width		= 7,
};

static struct c2c_dimension dim_rmt_hitm = {
	HEADER_SPAN_1("Rmt"),
	.name		= "rmt_hitm",
	.cmp		= rmt_hitm_cmp,
	.entry		= rmt_hitm_entry,
	.width		= 7,
};

static struct c2c_dimension dim_cl_rmt_hitm = {
	HEADER_CL_SPAN("----- HITM -----", "Rmt", 1),
	.name		= "cl_rmt_hitm",
	.cmp		= rmt_hitm_cmp,
	.entry		= rmt_hitm_entry,
	.width		= 7,
};

static struct c2c_dimension dim_cl_lcl_hitm = {
	HEADER_CL_SPAN_1("Lcl"),
	.name		= "cl_lcl_hitm",
	.cmp		= lcl_hitm_cmp,
	.entry		= lcl_hitm_entry,
	.width		= 7,
};

static struct c2c_dimension dim_stores = {
	HEADER_SPAN("---- Store Reference ----", "Total", 2),
	.name		= "stores",
	.cmp		= stores_cmp,
	.entry		= stores_entry,
	.width		= 7,
};

static struct c2c_dimension dim_stores_l1hit = {
	HEADER_SPAN_1("L1Hit"),
	.name		= "stores_l1hit",
	.cmp		= stores_l1hit_cmp,
	.entry		= stores_l1hit_entry,
	.width		= 7,
};

static struct c2c_dimension dim_stores_l1miss = {
	HEADER_SPAN_1("L1Miss"),
	.name		= "stores_l1miss",
	.cmp		= stores_l1miss_cmp,
	.entry		= stores_l1miss_entry,
	.width		= 7,
};

static struct c2c_dimension dim_cl_stores_l1hit = {
	HEADER_CL_SPAN("-- Store Refs --", "L1 Hit", 1),
	.name		= "cl_stores_l1hit",
	.cmp		= stores_l1hit_cmp,
	.entry		= stores_l1hit_entry,
	.width		= 7,
};

static struct c2c_dimension dim_cl_stores_l1miss = {
	HEADER_CL_SPAN_1("L1 Miss"),
	.name		= "cl_stores_l1miss",
	.cmp		= stores_l1miss_cmp,
	.entry		= stores_l1miss_entry,
	.width		= 7,
};

static struct c2c_dimension dim_ld_fbhit = {
	HEADER_SPAN("----- Core Load Hit -----", "FB", 2),
	.name		= "ld_fbhit",
	.cmp		= ld_fbhit_cmp,
	.entry		= ld_fbhit_entry,
	.width		= 7,
};

static struct c2c_dimension dim_ld_l1hit = {
	HEADER_SPAN_1("L1"),
	.name		= "ld_l1hit",
	.cmp		= ld_l1hit_cmp,
	.entry		= ld_l1hit_entry,
	.width		= 7,
};

static struct c2c_dimension dim_ld_l2hit = {
	HEADER_SPAN_1("L2"),
	.name		= "ld_l2hit",
	.cmp		= ld_l2hit_cmp,
	.entry		= ld_l2hit_entry,
	.width		= 7,
};

static struct c2c_dimension dim_ld_llchit = {
	HEADER_SPAN("-- LLC Load Hit --", "Llc", 1),
	.name		= "ld_lclhit",
	.cmp		= ld_llchit_cmp,
	.entry		= ld_llchit_entry,
	.width		= 8,
};

static struct c2c_dimension dim_ld_rmthit = {
	HEADER_SPAN_1("Rmt"),
	.name		= "ld_rmthit",
	.cmp		= ld_rmthit_cmp,
	.entry		= ld_rmthit_entry,
	.width		= 8,
};

static struct c2c_dimension dim_ld_llcmiss = {
	HEADER_1("LLC", "Ld Miss"),
	.name		= "ld_llcmiss",
	.cmp		= ld_llcmiss_cmp,
	.entry		= ld_llcmiss_entry,
	.width		= 7,
};

static struct c2c_dimension dim_tot_recs = {
	HEADER_1("Total", "records"),
	.name		= "tot_recs",
	.cmp		= tot_recs_cmp,
	.entry		= tot_recs_entry,
	.width		= 7,
};

static struct c2c_dimension dim_tot_loads = {
	HEADER_1("Total", "Loads"),
	.name		= "tot_loads",
	.cmp		= tot_loads_cmp,
	.entry		= tot_loads_entry,
	.width		= 7,
};

static struct c2c_dimension dim_percent_hitm = {
	HEADER_0("%hitm"),
	.name		= "percent_hitm",
	.cmp		= percent_hitm_cmp,
	.entry		= percent_hitm_entry,
	.color		= percent_hitm_color,
	.width		= 7,
};

static struct c2c_dimension dim_percent_ldmiss = {
	HEADER_1("%All","Ld Miss"),
	.name		= "percent_ldmiss",
	.cmp		= percent_ldmiss_cmp,
	.entry		= percent_ldmiss_entry,
	.color		= percent_ldmiss_color,
	.width		= 7,
};

static struct c2c_dimension dim_percent_rmt_hitm = {
	HEADER_OFF_SPAN("----- HITM -----", "Rmt", 1),
	.name		= "percent_rmt_hitm",
	.cmp		= percent_rmt_hitm_cmp,
	.entry		= percent_rmt_hitm_entry,
	.color		= percent_rmt_hitm_color,
	.width		= 7,
};

static struct c2c_dimension dim_percent_lcl_hitm = {
	HEADER_OFF_SPAN_1("Lcl"),
	.name		= "percent_lcl_hitm",
	.cmp		= percent_lcl_hitm_cmp,
	.entry		= percent_lcl_hitm_entry,
	.color		= percent_lcl_hitm_color,
	.width		= 7,
};

static struct c2c_dimension dim_percent_stores_l1hit = {
	HEADER_OFF_SPAN("-- Store Refs --", "L1 Hit", 1),
	.name		= "percent_stores_l1hit",
	.cmp		= percent_stores_l1hit_cmp,
	.entry		= percent_stores_l1hit_entry,
	.color		= percent_stores_l1hit_color,
	.width		= 7,
};

static struct c2c_dimension dim_percent_stores_l1miss = {
	HEADER_OFF_SPAN_1("L1 Miss"),
	.name		= "percent_stores_l1miss",
	.cmp		= percent_stores_l1miss_cmp,
	.entry		= percent_stores_l1miss_entry,
	.color		= percent_stores_l1miss_color,
	.width		= 7,
};

static struct c2c_dimension dim_dram_lcl = {
	HEADER_OFF_SPAN("--- Load Dram ----", "Lcl", 1),
	.name		= "dram_lcl",
	.cmp		= dram_lcl_cmp,
	.entry		= dram_lcl_entry,
	.width		= 8,
};

static struct c2c_dimension dim_dram_rmt = {
	HEADER_OFF_SPAN("-- Load Dram --", "Rmt", 1),
	.name		= "dram_rmt",
	.cmp		= dram_rmt_cmp,
	.entry		= dram_rmt_entry,
	.width		= 8,
};

static struct c2c_dimension dim_pid = {
	HEADER_CL_0("Pid"),
	.name		= "pid",
	.cmp		= pid_cmp,
	.entry		= pid_entry,
	.width		= 7,
};

static struct c2c_dimension dim_tid = {
	HEADER_CL_0("Tid"),
	.name		= "tid",
	.se		= &sort_thread,
};

static struct c2c_dimension dim_symbol = {
	.name		= "symbol",
	.se		= &sort_sym,
};

static struct c2c_dimension dim_dso = {
	.name		= "dso",
	.se		= &sort_dso,
};

static struct c2c_dimension dim_node = {
	HEADER_CL_0("Node"),
	.name		= "node",
	.cmp		= empty_cmp,
	.entry		= node_entry,
	.width		= 4,
};

static struct c2c_dimension dim_median = {
	HEADER_OFF_SPAN("----- cycles -----", "median", 1),
	.name		= "median",
	.cmp		= empty_cmp,
	.entry		= median_entry,
	.width		= 8,
};

static struct c2c_dimension dim_mean = {
	HEADER_OFF_SPAN_1("mean"),
	.name		= "mean",
	.cmp		= empty_cmp,
	.entry		= mean_entry,
	.width		= 8,
};

static struct c2c_dimension dim_stddev = {
	HEADER_OFF_0("CV"),
	.name		= "stddev",
	.cmp		= empty_cmp,
	.entry		= stddev_entry,
	.width		= 8,
};

static struct c2c_dimension dim_cpucnt = {
	HEADER_CL_1("cpu", "cnt"),
	.name		= "cpucnt",
	.cmp		= empty_cmp,
	.entry		= cpucnt_entry,
	.width		= 8,
};

#undef HEADER_0
#undef HEADER_1
#undef HEADER_SPAN
#undef HEADER_SPAN_1

#undef HEADER_CL_0
#undef HEADER_CL_1

#undef HEADER_OFF_0

static struct c2c_dimension *dimensions[] = {
	&dim_dcacheline,
	&dim_offset,
	&dim_iaddr,
	&dim_dsymbol,
	&dim_tot_hitm,
	&dim_lcl_hitm,
	&dim_rmt_hitm,
	&dim_cl_lcl_hitm,
	&dim_cl_rmt_hitm,
	&dim_stores,
	&dim_stores_l1hit,
	&dim_stores_l1miss,
	&dim_cl_stores_l1hit,
	&dim_cl_stores_l1miss,
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
	&dim_dram_lcl,
	&dim_dram_rmt,
	&dim_pid,
	&dim_tid,
	&dim_symbol,
	&dim_dso,
	&dim_node,
	&dim_median,
	&dim_mean,
	&dim_stddev,
	&dim_cpucnt,
	NULL,
};

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

static int c2c_se_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			struct hist_entry *he)
{
	struct c2c_fmt *c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	struct c2c_dimension *dim = c2c_fmt->dim;
	size_t len = fmt->user_len;

	if (!len)
		len = hists__col_len(he->hists, dim->se->se_width_idx);

	return dim->se->se_snprintf(he, hpp->buf, hpp->size, len);
}

static int64_t c2c_se_cmp(struct perf_hpp_fmt *fmt,
			  struct hist_entry *a, struct hist_entry *b)
{
	struct c2c_fmt *c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	struct c2c_dimension *dim = c2c_fmt->dim;

	return dim->se->se_cmp(a, b);
}

static int64_t c2c_se_collapse(struct perf_hpp_fmt *fmt,
			       struct hist_entry *a, struct hist_entry *b)
{
	struct c2c_fmt *c2c_fmt = container_of(fmt, struct c2c_fmt, fmt);
	struct c2c_dimension *dim = c2c_fmt->dim;
	int64_t (*collapse_fn)(struct hist_entry *, struct hist_entry *);

	collapse_fn = dim->se->se_collapse ?: dim->se->se_cmp;
	return collapse_fn(a, b);
}

static struct c2c_fmt *get_format(const char *name)
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

	fmt->cmp	= dim->se ? c2c_se_cmp   : dim->cmp;
	fmt->sort	= dim->se ? c2c_se_cmp   : dim->cmp;
	fmt->color	= dim->se ? NULL	 : dim->color;
	fmt->entry	= dim->se ? c2c_se_entry : dim->entry;
	fmt->header	= c2c_header;
	fmt->width	= c2c_width;
	fmt->collapse	= dim->se ? c2c_se_collapse : dim->cmp;
	fmt->equal	= fmt_equal;
	fmt->free	= fmt_free;

	return c2c_fmt;
}

static int c2c_hists__init_output(struct perf_hpp_list *hpp_list, char *name)
{
	struct c2c_fmt *c2c_fmt = get_format(name);

	if (!c2c_fmt) {
		reset_dimensions();
		return output_field_add(hpp_list, name);
	}

	perf_hpp_list__column_register(hpp_list, &c2c_fmt->fmt);
	return 0;
}

static int c2c_hists__init_sort(struct perf_hpp_list *hpp_list, char *name)
{
	struct c2c_fmt *c2c_fmt = get_format(name);
	struct c2c_dimension *dim;

	if (!c2c_fmt) {
		reset_dimensions();
		return sort_dimension__add(hpp_list, name, NULL, 0);
	}

	dim = c2c_fmt->dim;
	if (dim == &dim_dso)
		hpp_list->dso = 1;

	perf_hpp_list__register_sort_field(hpp_list, &c2c_fmt->fmt);
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
			ret = _fn(hpp_list, tok);				\
			if (ret == -EINVAL) {					\
				error("Invalid --fields key: `%s'", tok);	\
				break;						\
			} else if (ret == -ESRCH) {				\
				error("Unknown --fields key: `%s'", tok);	\
				break;						\
			}							\
		}								\
	} while (0)

static int hpp_list__parse(struct perf_hpp_list *hpp_list,
			   const char *output_,
			   const char *sort_)
{
	char *output = output_ ? strdup(output_) : NULL;
	char *sort   = sort_   ? strdup(sort_) : NULL;
	int ret;

	PARSE_LIST(output, c2c_hists__init_output);
	PARSE_LIST(sort,   c2c_hists__init_sort);

	/* copy sort keys to output fields */
	perf_hpp__setup_output_field(hpp_list);

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
			   const char *sort,
			   int nr_header_lines)
{
	__hists__init(&hists->hists, &hists->list);

	/*
	 * Initialize only with sort fields, we need to resort
	 * later anyway, and that's where we add output fields
	 * as well.
	 */
	perf_hpp_list__init(&hists->list);

	/* Overload number of header lines.*/
	hists->list.nr_header_lines = nr_header_lines;

	return hpp_list__parse(&hists->list, NULL, sort);
}

__maybe_unused
static int c2c_hists__reinit(struct c2c_hists *c2c_hists,
			     const char *output,
			     const char *sort)
{
	perf_hpp__reset_output_field(&c2c_hists->list);
	return hpp_list__parse(&c2c_hists->list, output, sort);
}

#define DISPLAY_LINE_LIMIT  0.0005

enum {
	DISPLAY_LCL,
	DISPLAY_RMT,
	DISPLAY_ALL,
};

static bool he__display(struct hist_entry *he, struct c2c_stats *stats)
{
	struct c2c_hist_entry *c2c_he;
	double ld_dist;

	c2c_he = container_of(he, struct c2c_hist_entry, he);

	switch (c2c.display) {
	case DISPLAY_LCL:
		if (stats->lcl_hitm) {
			ld_dist = ((double)c2c_he->stats.lcl_hitm / stats->lcl_hitm);
			if (ld_dist < DISPLAY_LINE_LIMIT)
				he->filtered = HIST_FILTER__C2C;
		} else {
			he->filtered = HIST_FILTER__C2C;
		}
		break;
	case DISPLAY_RMT:
		if (stats->rmt_hitm) {
			ld_dist = ((double)c2c_he->stats.rmt_hitm / stats->rmt_hitm);
			if (ld_dist < DISPLAY_LINE_LIMIT)
				he->filtered = HIST_FILTER__C2C;
		} else {
			he->filtered = HIST_FILTER__C2C;
		}
		break;
	case DISPLAY_ALL:
	default:
		break;
	};

	return he->filtered == 0;
}

static inline int valid_hitm_or_store(struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	return c2c_he->stats.lcl_hitm || c2c_he->stats.rmt_hitm || c2c_he->stats.store;
}

static int filter_cb(struct hist_entry *he)
{
	if (!valid_hitm_or_store(he))
		he->filtered = HIST_FILTER__C2C;

	return 0;
}

static int resort_cl_cb(struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	struct c2c_hists *c2c_hists;
	bool display = he__display(he, &c2c.hitm_stats);

	c2c_he = container_of(he, struct c2c_hist_entry, he);
	c2c_hists = c2c_he->hists;

	if (display && c2c_hists) {
		c2c_hists__reinit(c2c_hists,
			"percent_rmt_hitm,"
			"percent_lcl_hitm,"
			"percent_stores_l1hit,"
			"percent_stores_l1miss,"
			"offset,"
			"pid,"
			"tid,"
			"median,"
			"mean,"
			"stddev,"
			"cpucnt,"
			"symbol,"
			"dso,"
			"node",
			"offset,rmt_hitm,lcl_hitm");

		hists__collapse_resort(&c2c_hists->hists, NULL);
		hists__output_resort_cb(&c2c_hists->hists, NULL, filter_cb);
	}

	return 0;
}

#define HAS_HITMS(__h) ((__h)->stats.lcl_hitm || (__h)->stats.rmt_hitm)

static int resort_hitm_cb(struct hist_entry *he)
{
	struct c2c_hist_entry *c2c_he;
	c2c_he = container_of(he, struct c2c_hist_entry, he);

	if (HAS_HITMS(c2c_he)) {
		c2c.shared_clines++;
		c2c_add_stats(&c2c.hitm_stats, &c2c_he->stats);
	}

	return 0;
}

static int hists__iterate_cb(struct hists *hists, hists__resort_cb_t cb)
{
	struct rb_node *next = rb_first(&hists->entries);
	int ret = 0;

	while (next) {
		struct hist_entry *he;

		he = rb_entry(next, struct hist_entry, rb_node);
		ret = cb(he);
		if (ret)
			break;
		next = rb_next(&he->rb_node);
	}

	return ret;
}

static void print_c2c__display_stats(void)
{
	int llc_misses;
	struct c2c_stats *stats = &c2c.hists.stats;

	llc_misses = stats->lcl_dram +
		     stats->rmt_dram +
		     stats->rmt_hit +
		     stats->rmt_hitm;

	printf("=================================================\n");
	printf("            Trace Event Information              \n");
	printf("=================================================\n");
	printf("  Total records                     : %10d\n", stats->nr_entries);
	printf("  Locked Load/Store Operations      : %10d\n", stats->locks);
	printf("  Load Operations                   : %10d\n", stats->load);
	printf("  Loads - uncacheable               : %10d\n", stats->ld_uncache);
	printf("  Loads - IO                        : %10d\n", stats->ld_io);
	printf("  Loads - Miss                      : %10d\n", stats->ld_miss);
	printf("  Loads - no mapping                : %10d\n", stats->ld_noadrs);
	printf("  Load Fill Buffer Hit              : %10d\n", stats->ld_fbhit);
	printf("  Load L1D hit                      : %10d\n", stats->ld_l1hit);
	printf("  Load L2D hit                      : %10d\n", stats->ld_l2hit);
	printf("  Load LLC hit                      : %10d\n", stats->ld_llchit + stats->lcl_hitm);
	printf("  Load Local HITM                   : %10d\n", stats->lcl_hitm);
	printf("  Load Remote HITM                  : %10d\n", stats->rmt_hitm);
	printf("  Load Remote HIT                   : %10d\n", stats->rmt_hit);
	printf("  Load Local DRAM                   : %10d\n", stats->lcl_dram);
	printf("  Load Remote DRAM                  : %10d\n", stats->rmt_dram);
	printf("  Load MESI State Exclusive         : %10d\n", stats->ld_excl);
	printf("  Load MESI State Shared            : %10d\n", stats->ld_shared);
	printf("  Load LLC Misses                   : %10d\n", llc_misses);
	printf("  LLC Misses to Local DRAM          : %10.1f%%\n", ((double)stats->lcl_dram/(double)llc_misses) * 100.);
	printf("  LLC Misses to Remote DRAM         : %10.1f%%\n", ((double)stats->rmt_dram/(double)llc_misses) * 100.);
	printf("  LLC Misses to Remote cache (HIT)  : %10.1f%%\n", ((double)stats->rmt_hit /(double)llc_misses) * 100.);
	printf("  LLC Misses to Remote cache (HITM) : %10.1f%%\n", ((double)stats->rmt_hitm/(double)llc_misses) * 100.);
	printf("  Store Operations                  : %10d\n", stats->store);
	printf("  Store - uncacheable               : %10d\n", stats->st_uncache);
	printf("  Store - no mapping                : %10d\n", stats->st_noadrs);
	printf("  Store L1D Hit                     : %10d\n", stats->st_l1hit);
	printf("  Store L1D Miss                    : %10d\n", stats->st_l1miss);
	printf("  No Page Map Rejects               : %10d\n", stats->nomap);
	printf("  Unable to parse data source       : %10d\n", stats->noparse);
}

static void print_shared_cacheline_info(void)
{
	struct c2c_stats *stats = &c2c.hitm_stats;
	int hitm_cnt = stats->lcl_hitm + stats->rmt_hitm;

	printf("=================================================\n");
	printf("    Global Shared Cache Line Event Information   \n");
	printf("=================================================\n");
	printf("  Total Shared Cache Lines          : %10d\n", c2c.shared_clines);
	printf("  Load HITs on shared lines         : %10d\n", stats->load);
	printf("  Fill Buffer Hits on shared lines  : %10d\n", stats->ld_fbhit);
	printf("  L1D hits on shared lines          : %10d\n", stats->ld_l1hit);
	printf("  L2D hits on shared lines          : %10d\n", stats->ld_l2hit);
	printf("  LLC hits on shared lines          : %10d\n", stats->ld_llchit + stats->lcl_hitm);
	printf("  Locked Access on shared lines     : %10d\n", stats->locks);
	printf("  Store HITs on shared lines        : %10d\n", stats->store);
	printf("  Store L1D hits on shared lines    : %10d\n", stats->st_l1hit);
	printf("  Total Merged records              : %10d\n", hitm_cnt + stats->store);
}

static void print_cacheline(struct c2c_hists *c2c_hists,
			    struct hist_entry *he_cl,
			    struct perf_hpp_list *hpp_list,
			    FILE *out)
{
	char bf[1000];
	struct perf_hpp hpp = {
		.buf            = bf,
		.size           = 1000,
	};
	static bool once;

	if (!once) {
		hists__fprintf_headers(&c2c_hists->hists, out);
		once = true;
	} else {
		fprintf(out, "\n");
	}

	fprintf(out, "  --------------------------------------------------------\n");
	hist_entry__snprintf(he_cl, &hpp, hpp_list);
	fprintf(out, "%s\n", bf);
	fprintf(out, "  --------------------------------------------------------\n");

	hists__fprintf(&c2c_hists->hists, false, 0, 0, 0, out, true);
}

static void print_pareto(FILE *out)
{
	struct perf_hpp_list hpp_list;
	struct rb_node *nd;
	int ret;

	perf_hpp_list__init(&hpp_list);
	ret = hpp_list__parse(&hpp_list,
				"cl_rmt_hitm,"
				"cl_lcl_hitm,"
				"cl_stores_l1hit,"
				"cl_stores_l1miss,"
				"dcacheline",
				NULL);

	if (WARN_ONCE(ret, "failed to setup sort entries\n"))
		return;

	nd = rb_first(&c2c.hists.hists.entries);

	for (; nd; nd = rb_next(nd)) {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);
		struct c2c_hist_entry *c2c_he;

		if (he->filtered)
			continue;

		c2c_he = container_of(he, struct c2c_hist_entry, he);
		print_cacheline(c2c_he->hists, he, &hpp_list, out);
	}
}

static void perf_c2c__hists_fprintf(FILE *out)
{
	setup_pager();

	print_c2c__display_stats();
	fprintf(out, "\n");
	print_shared_cacheline_info();

	if (c2c.stats_only)
		return;

	fprintf(out, "\nShared Data Cache Line Table\n\n");
	hists__fprintf(&c2c.hists.hists, true, 0, 0, 0, stdout, false);

	fprintf(out, "\nShared Cache Line Distribution Pareto\n\n");
	print_pareto(out);
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
                addr = cl_address(he->mem_info->daddr.al_addr);

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
		browser->hb.c2c_filter	= true;
		browser->hb.title	= perf_c2c_cacheline_browser__title;
		browser->he		= he;
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
		  "Shared Data Cache Line Table "
		  "(%lu entries)", browser->nr_non_filtered_entries);
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

static void ui_quirks(bool stdio)
{
	if (!stdio)
		dim_offset.width = 5;
}

#define CALLCHAIN_DEFAULT_OPT  "graph,0.5,caller,function,percent"

const char callchain_help[] = "Display call graph (stack chain/backtrace):\n\n"
				CALLCHAIN_REPORT_HELP
				"\n\t\t\t\tDefault: " CALLCHAIN_DEFAULT_OPT;

static int
parse_callchain_opt(const struct option *opt, const char *arg, int unset)
{
	struct callchain_param *callchain = opt->value;

	callchain->enabled = !unset;
	/*
	 * --no-call-graph
	 */
	if (unset) {
		symbol_conf.use_callchain = false;
		callchain->mode = CHAIN_NONE;
		return 0;
	}

	return parse_callchain_report_opt(arg);
}

static int setup_callchain(struct perf_evlist *evlist)
{
	u64 sample_type = perf_evlist__combined_sample_type(evlist);
	enum perf_call_graph_mode mode = CALLCHAIN_NONE;

	if ((sample_type & PERF_SAMPLE_REGS_USER) &&
	    (sample_type & PERF_SAMPLE_STACK_USER))
		mode = CALLCHAIN_DWARF;
	else if (sample_type & PERF_SAMPLE_BRANCH_STACK)
		mode = CALLCHAIN_LBR;
	else if (sample_type & PERF_SAMPLE_CALLCHAIN)
		mode = CALLCHAIN_FP;

	if (!callchain_param.enabled &&
	    callchain_param.mode != CHAIN_NONE &&
	    mode != CALLCHAIN_NONE) {
		symbol_conf.use_callchain = true;
		if (callchain_register_param(&callchain_param) < 0) {
			ui__error("Can't register callchain params.\n");
			return -EINVAL;
		}
	}

	callchain_param.record_mode = mode;
	callchain_param.min_percent = 0;
	return 0;
}

static int setup_display(const char *str)
{
	const char *display = str ?: "rmt";

	if (!strcmp(display, "rmt"))
		c2c.display = DISPLAY_RMT;
	else if (!strcmp(display, "lcl"))
		c2c.display = DISPLAY_LCL;
	else if (!strcmp(display, "all"))
		c2c.display = DISPLAY_ALL;
	else {
		pr_err("failed: unknown display type: %s\n", str);
		return -1;
	}

	return 0;
}

static int perf_c2c__report(int argc, const char **argv)
{
	struct perf_session *session;
	struct ui_progress prog;
	struct perf_data_file file = {
		.mode = PERF_DATA_MODE_READ,
	};
	char callchain_default_opt[] = CALLCHAIN_DEFAULT_OPT;
	const char *display = NULL;
	const struct option c2c_options[] = {
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_INCR('v', "verbose", &verbose,
		 "be more verbose (show counter open errors, etc)"),
	OPT_STRING('i', "input", &input_name, "file",
		   "the input file to process"),
	OPT_INCR('N', "node-info", &c2c.node_info,
		 "show extra node info in report (repeat for more info)"),
	OPT_BOOLEAN(0, "stdio", &c2c.use_stdio,
		    "Use the stdio interface"),
	OPT_BOOLEAN(0, "stats", &c2c.stats_only,
		    "Use the stdio interface"),
	OPT_CALLBACK_DEFAULT('g', "call-graph", &callchain_param,
			     "print_type,threshold[,print_limit],order,sort_key[,branch],value",
			     callchain_help, &parse_callchain_opt,
			     callchain_default_opt),
	OPT_STRING(0, "display", &display, NULL, "lcl,rmt,all"),
	OPT_END()
	};
	int err = 0;

	argc = parse_options(argc, argv, c2c_options, report_c2c_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc)
		usage_with_options(report_c2c_usage, c2c_options);

	if (c2c.stats_only)
		c2c.use_stdio = true;

	if (c2c.use_stdio)
		use_browser = 0;
	else
		use_browser = 1;

	ui_quirks(c2c.use_stdio);

	setup_browser(false);

	if (!input_name || !strlen(input_name))
		input_name = "perf.data";

	file.path = input_name;

	err = setup_display(display);
	if (err)
		goto out;

	err = c2c_hists__init(&c2c.hists, "dcacheline", 2);
	if (err) {
		pr_debug("Failed to initialize hists\n");
		goto out;
	}

	session = perf_session__new(&file, 0, &c2c.tool);
	if (session == NULL) {
		pr_debug("No memory for session\n");
		goto out;
	}

	err = setup_nodes(session);
	if (err) {
		pr_err("Failed setup nodes\n");
		goto out;
	}

	err = setup_callchain(session->evlist);
	if (err)
		goto out_session;

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

	c2c_hists__reinit(&c2c.hists,
			"dcacheline,"
			"tot_recs,"
			"percent_ldmiss,"
			"percent_hitm,"
			"tot_loads,"
			"ld_fbhit,"
			"ld_l1hit,"
			"ld_l2hit,"
			"ld_lclhit,ld_rmthit,"
			"tot_hitm,lcl_hitm,rmt_hitm,"
			"dram_lcl,dram_rmt,"
			"ld_llcmiss,"
			"stores,"
			"stores_l1hit,"
			"stores_l1miss",
			"rmt_hitm"
			);

	ui_progress__init(&prog, c2c.hists.hists.nr_entries, "Sorting...");

	hists__collapse_resort(&c2c.hists.hists, NULL);
	hists__output_resort_cb(&c2c.hists.hists, &prog, resort_hitm_cb);
	hists__iterate_cb(&c2c.hists.hists, resort_cl_cb);

	ui_progress__finish();

	if (c2c.use_stdio)
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
