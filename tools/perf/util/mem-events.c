#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <api/fs/fs.h>
#include "mem-events.h"
#include "debug.h"
#include "sort.h"

#define E(t, n, s) { .tag = t, .name = n, .sysfs_name = s }

struct perf_mem_event perf_mem_events[PERF_MEM_EVENTS__MAX] = {
	E("ldlat-loads",	"cpu/mem-loads,ldlat=30/P",	"mem-loads"),
	E("ldlat-stores",	"cpu/mem-stores/P",		"mem-stores"),
	E("stlb-miss-loads",	"cpu/mem-stlb-miss-loads/P",	"mem-stlb-miss-loads"),
	E("stlb-miss-stores",	"cpu/mem-stlb-miss-stores/P",	"mem-stlb-miss-stores"),
	E("lock-loads",		"cpu/mem-lock-loads/P",		"mem-lock-loads"),
	E("split-loads",	"cpu/mem-split-loads/P",	"mem-split-loads"),
	E("split-stores",	"cpu/mem-split-stores/P",	"mem-split-stores"),
	E("all-loads",		"cpu/mem-all-loads/P",		"mem-all-loads"),
	E("all-stores",		"cpu/mem-all-stores/P",		"mem-all-stores"),
	E("l1-hit",		"cpu/mem-load-l1-hit/P",	"mem-load-l1-hit"),
	E("l2-hit",		"cpu/mem-load-l2-hit/P",	"mem-load-l2-hit"),
	E("l3-hit",		"cpu/mem-load-l3-hit/P",	"mem-load-l3-hit"),
	E("l1-miss",		"cpu/mem-load-l1-miss/P",	"mem-load-l1-miss"),
	E("l2-miss",		"cpu/mem-load-l2-miss/P",	"mem-load-l2-miss"),
	E("l3-miss",		"cpu/mem-load-l3-miss/P",	"mem-load-l3-miss"),
	E("lfb",		"cpu/mem-load-hit-lfb/P",	"mem-load-hit-lfb"),
	E("snp-miss",		"cpu/mem-snp-miss/P",		"mem-snp-miss"),
	E("snp-hit",		"cpu/mem-snp-hit/P",		"mem-snp-hit"),
	E("snp-hitm",		"cpu/mem-snp-hitm/P",		"mem-snp-hitm"),
	E("snp-none",		"cpu/mem-snp-none/P",		"mem-snp-none"),
	E("local-dram",		"cpu/mem-local-dram/P",		"mem-local-dram"),
};
#undef E

#undef E

char *perf_mem_events__name(int i)
{
	return (char *) perf_mem_events[i].name;
}

int perf_mem_events__parse(const char *str)
{
	char *tok, *saveptr = NULL;
	bool found = false;
	char *buf;
	int j;

	/* We need buffer that we know we can write to. */
	buf = malloc(strlen(str) + 1);
	if (!buf)
		return -ENOMEM;

	strcpy(buf, str);

	tok = strtok_r((char *)buf, ",", &saveptr);

	while (tok) {
		for (j = 0; j < PERF_MEM_EVENTS__MAX; j++) {
			struct perf_mem_event *e = &perf_mem_events[j];

			if (strstr(e->tag, tok))
				e->record = found = true;
		}

		tok = strtok_r(NULL, ",", &saveptr);
	}

	free(buf);

	if (found)
		return 0;

	pr_err("event '%s' not found, ", str);
	return -1;
}

int perf_mem_events__init(void)
{
	const char *mnt = sysfs__mount();
	bool found = false;
	int j;

	if (!mnt)
		return -ENOENT;

	for (j = 0; j < PERF_MEM_EVENTS__MAX; j++) {
		char path[PATH_MAX];
		struct perf_mem_event *e = &perf_mem_events[j];
		struct stat st;

		scnprintf(path, PATH_MAX, "%s/devices/cpu/events/%s",
			  mnt, e->sysfs_name);

		if (!stat(path, &st))
			e->supported = found = true;
	}

	return found ? 0 : -ENOENT;
}

int perf_mem_events__find(char *name)
{
	unsigned i;

	for (i = 0; i < PERF_MEM_EVENTS__MAX; i++) {
		struct perf_mem_event *event = &perf_mem_events[i];

		if (strstr(name, event->sysfs_name)) {

			/*
			 * Check for hit substring in hitm and
			 * return hitm index if we match it.
			 */
			if (i != PERF_MEM_EVENTS__SNP_HIT)
				break;

			event = &perf_mem_events[PERF_MEM_EVENTS__SNP_HITM];
			if (strstr(name, event->sysfs_name))
				i++;
			break;
		}
	}

	return i;
}

enum { NA = -1, OP, LVL, SNP, LCK, TLB };

static int data_src__scnprintf(char *bf, size_t size, uint64_t val, int64_t field)
{
#define SEPARATOR	"|"
#define ELLIPSIS     "..."
	static const struct {
		uint64_t   bit;
		int64_t    field;
		const char *name;
	} decode_bits[] = {
	{ PERF_MEM_OP_LOAD,       OP,  "LOAD"     },
	{ PERF_MEM_OP_STORE,      OP,  "STORE"    },
	{ PERF_MEM_OP_NA,         OP,  "OP_NA"    },
	{ PERF_MEM_LVL_LFB,       LVL, "LFB"      },
	{ PERF_MEM_LVL_L1,        LVL, "L1"       },
	{ PERF_MEM_LVL_L2,        LVL, "L2"       },
	{ PERF_MEM_LVL_L3,        LVL, "LCL_LLC"  },
	{ PERF_MEM_LVL_LOC_RAM,   LVL, "LCL_RAM"  },
	{ PERF_MEM_LVL_REM_RAM1,  LVL, "RMT_RAM"  },
	{ PERF_MEM_LVL_REM_RAM2,  LVL, "RMT_RAM"  },
	{ PERF_MEM_LVL_REM_CCE1,  LVL, "RMT_LLC"  },
	{ PERF_MEM_LVL_REM_CCE2,  LVL, "RMT_LLC"  },
	{ PERF_MEM_LVL_IO,        LVL, "I/O"      },
	{ PERF_MEM_LVL_UNC,       LVL, "UNCACHED" },
	{ PERF_MEM_LVL_NA,        LVL, "NA"       },
	{ PERF_MEM_LVL_HIT,       LVL, "HIT"      },
	{ PERF_MEM_LVL_MISS,      LVL, "MISS"     },
	{ PERF_MEM_SNOOP_NONE,    SNP, "SNP NONE" },
	{ PERF_MEM_SNOOP_HIT,     SNP, "SNP HIT"  },
	{ PERF_MEM_SNOOP_MISS,    SNP, "SNP MISS" },
	{ PERF_MEM_SNOOP_HITM,    SNP, "SNP HITM" },
	{ PERF_MEM_SNOOP_NA,      SNP, "SNP NA"   },
	{ PERF_MEM_LOCK_LOCKED,   LCK, "LOCKED"   },
	{ PERF_MEM_LOCK_NA,       LCK, "LOCK_NA"  },
	{ PERF_MEM_TLB_NA,        TLB, "TLB_NA"   },
	{ PERF_MEM_TLB_HIT,       TLB, "TLB_HIT"  },
	{ PERF_MEM_TLB_MISS,      TLB, "TLB_MISS" },
	{ PERF_MEM_TLB_L1,        TLB, "TLB_L1"   },
	{ PERF_MEM_TLB_L2,        TLB, "TLB_L2"   },
	{ PERF_MEM_TLB_WK,        TLB, "WALKER"   },
	{ PERF_MEM_TLB_OS,        TLB, "FAULT"    },
	};
	union perf_mem_data_src dsrc = { .val = val, };
	int printed = 0;
	size_t i;
	bool first_present = true;

	bf[0] = 0;

	for (i = 0; i < ARRAY_SIZE(decode_bits); i++) {
		int bitval;

		if (field != NA && decode_bits[i].field != field)
			continue;

		switch (decode_bits[i].field) {
		case OP:  bitval = decode_bits[i].bit & dsrc.mem_op;    break;
		case LVL: bitval = decode_bits[i].bit & dsrc.mem_lvl;   break;
		case SNP: bitval = decode_bits[i].bit & dsrc.mem_snoop; break;
		case LCK: bitval = decode_bits[i].bit & dsrc.mem_lock;  break;
		case TLB: bitval = decode_bits[i].bit & dsrc.mem_dtlb;  break;
		default: bitval = 0;					break;
		}

		if (!bitval)
			continue;

		if (strlen(decode_bits[i].name) + !!i > size - printed) {
			sprintf(bf + size - sizeof(ELLIPSIS) + 1, ELLIPSIS);
			printed = size;
			break;
		}

		printed += scnprintf(bf + printed, size - printed, "%s%s",
				     first_present ? "" : SEPARATOR, decode_bits[i].name);
		first_present = false;
	}

	return printed;
}

int perf_mem__op_scnprintf(char *bf, size_t size, uint64_t val)
{
	return data_src__scnprintf(bf, size, val, OP);
}

int perf_mem__lvl_scnprintf(char *bf, size_t size, uint64_t val)
{
	return data_src__scnprintf(bf, size, val, LVL);
}

int perf_mem__tlb_scnprintf(char *bf, size_t size, uint64_t val)
{
	return data_src__scnprintf(bf, size, val, TLB);
}

int perf_mem__snp_scnprintf(char *bf, size_t size, uint64_t val)
{
	return data_src__scnprintf(bf, size, val, SNP);
}

int perf_mem__lock_scnprintf(char *bf, size_t size, uint64_t val)
{
	return data_src__scnprintf(bf, size, val, LCK);
}

int c2c_decode_stats(struct c2c_stats *stats, struct hist_entry *entry)
{
	union perf_mem_data_src *data_src = &entry->mem_info->data_src;
	u64 daddr  = entry->mem_info->daddr.addr;
//	u64 weight = entry->stat.weight;
	u64 op     = data_src->mem_op;
	u64 lvl    = data_src->mem_lvl;
	u64 snoop  = data_src->mem_snoop;
	u64 lock   = data_src->mem_lock;
	int err = 0;

#define P(a,b) PERF_MEM_##a##_##b

	stats->nr_entries++;
	stats->total_period += entry->stat.period;

	if (lock & P(LOCK,LOCKED)) stats->t.locks++;

	if (op & P(OP,LOAD)) {
		stats->t.load++;

		if (!daddr) {
			stats->t.ld_noadrs++;
			return -1;
		}

		if (lvl & P(LVL,HIT)) {
			if (lvl & P(LVL,UNC)) stats->t.ld_uncache++;
			if (lvl & P(LVL,IO))  stats->t.ld_io++;
			if (lvl & P(LVL,LFB)) stats->t.ld_fbhit++;
			if (lvl & P(LVL,L1 )) stats->t.ld_l1hit++;
			if (lvl & P(LVL,L2 )) stats->t.ld_l2hit++;
			if (lvl & P(LVL,L3 )) {
				if (snoop & P(SNOOP,HITM))
					stats->t.lcl_hitm++;
				else
					stats->t.ld_llchit++;
			}

			if (lvl & P(LVL,LOC_RAM)) {
				stats->t.lcl_dram++;
				if (snoop & P(SNOOP,HIT))
					stats->t.ld_shared++;
				else
					stats->t.ld_excl++;
			}

			if ((lvl & P(LVL,REM_RAM1)) ||
			    (lvl & P(LVL,REM_RAM2))) {
				stats->t.rmt_dram++;
				if (snoop & P(SNOOP,HIT))
					stats->t.ld_shared++;
				else
					stats->t.ld_excl++;
			}
		}

		if ((lvl & P(LVL,REM_CCE1)) ||
		    (lvl & P(LVL,REM_CCE2))) {
			if (snoop & P(SNOOP, HIT))
				stats->t.rmt_hit++;
			else if (snoop & P(SNOOP, HITM)) {
				stats->t.rmt_hitm++;
//				update_stats(&stats->stats, weight);
			}
		}

		if ((lvl & P(LVL,MISS)))
			stats->t.ld_miss++;

	} else if (op & P(OP,STORE)) {
		/* store */
		stats->t.store++;

		if (!daddr) {
			stats->t.st_noadrs++;
			return -1;
		}

		if (lvl & P(LVL,HIT)) {
			if (lvl & P(LVL,UNC)) stats->t.st_uncache++;
			if (lvl & P(LVL,L1 )) stats->t.st_l1hit++;
		}
		if (lvl & P(LVL,MISS))
			if (lvl & P(LVL,L1)) stats->t.st_l1miss++;
	} else {
		/* unparsable data_src? */
		stats->t.noparse++;
		return -1;
	}

	if (!entry->mem_info->daddr.map || !entry->mem_info->iaddr.map) {
		stats->t.nomap++;
		return -1;
	}

	return err;
}

uint64_t perf_c2c_stats__total_records(struct c2c_stats *stats)
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

double perf_c2c_stats__percent_hitm(struct c2c_stats *stats,
				 struct c2c_stats *total)
{
	int tot, st;
	double p;

	st  = stats->t.lcl_hitm + stats->t.rmt_hitm;
	tot = total->t.lcl_hitm + total->t.rmt_hitm;

	p = tot ? (double) st / tot : 0;

	return 100 * p;
}

double perf_c2c_stats__percent_ldmiss(struct c2c_stats *stats,
				      struct c2c_stats *total)
{
	int tot, st;
	double p;

	st  = stats->t.rmt_hitm;
        tot = total->t.lcl_dram +
              total->t.rmt_dram +
              total->t.rmt_hit +
              total->t.rmt_hitm;

	p = tot ? (double) st / tot : 0;

	return 100 * p;
}
