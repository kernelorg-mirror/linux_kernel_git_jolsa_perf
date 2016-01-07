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
#include "symbol.h"
#include "sort.h"

unsigned int perf_mem_events__loads_ldlat = 30;

#define E(t, n, s) { .tag = t, .name = n, .sysfs_name = s }

struct perf_mem_event perf_mem_events[PERF_MEM_EVENTS__MAX] = {
	E("ldlat-loads",	"cpu/mem-loads,ldlat=%u/P",	"mem-loads"),
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

static char mem_loads_name[100];
static bool mem_loads_name__init;

char *perf_mem_events__name(int i)
{
	if (i == PERF_MEM_EVENTS__LOAD) {
		if (!mem_loads_name__init) {
			mem_loads_name__init = true;
			scnprintf(mem_loads_name, sizeof(mem_loads_name),
				  perf_mem_events[i].name,
				  perf_mem_events__loads_ldlat);
		}
		return mem_loads_name;
	}

	return (char *)perf_mem_events[i].name;
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

	pr_err("failed: event '%s' not found, use '-e list' to get list of available events\n", str);
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

static const char * const tlb_access[] = {
	"N/A",
	"HIT",
	"MISS",
	"L1",
	"L2",
	"Walker",
	"Fault",
};

int perf_mem__tlb_scnprintf(char *out, size_t sz, struct mem_info *mem_info)
{
	size_t l = 0, i;
	u64 m = PERF_MEM_TLB_NA;
	u64 hit, miss;

	sz -= 1; /* -1 for null termination */
	out[0] = '\0';

	if (mem_info)
		m = mem_info->data_src.mem_dtlb;

	hit = m & PERF_MEM_TLB_HIT;
	miss = m & PERF_MEM_TLB_MISS;

	/* already taken care of */
	m &= ~(PERF_MEM_TLB_HIT|PERF_MEM_TLB_MISS);

	for (i = 0; m && i < ARRAY_SIZE(tlb_access); i++, m >>= 1) {
		if (!(m & 0x1))
			continue;
		if (l) {
			strcat(out, " or ");
			l += 4;
		}
		l += scnprintf(out + l, sz - l, tlb_access[i]);
	}
	if (*out == '\0')
		l += scnprintf(out, sz - l, "N/A");
	if (hit)
		l += scnprintf(out + l, sz - l, " hit");
	if (miss)
		l += scnprintf(out + l, sz - l, " miss");

	return l;
}

static const char * const mem_lvl[] = {
	"N/A",
	"HIT",
	"MISS",
	"L1",
	"LFB",
	"L2",
	"L3",
	"Local RAM",
	"Remote RAM (1 hop)",
	"Remote RAM (2 hops)",
	"Remote Cache (1 hop)",
	"Remote Cache (2 hops)",
	"I/O",
	"Uncached",
};

int perf_mem__lvl_scnprintf(char *out, size_t sz, struct mem_info *mem_info)
{
	size_t i, l = 0;
	u64 m =  PERF_MEM_LVL_NA;
	u64 hit, miss;

	if (mem_info)
		m  = mem_info->data_src.mem_lvl;

	sz -= 1; /* -1 for null termination */
	out[0] = '\0';

	hit = m & PERF_MEM_LVL_HIT;
	miss = m & PERF_MEM_LVL_MISS;

	/* already taken care of */
	m &= ~(PERF_MEM_LVL_HIT|PERF_MEM_LVL_MISS);

	for (i = 0; m && i < ARRAY_SIZE(mem_lvl); i++, m >>= 1) {
		if (!(m & 0x1))
			continue;
		if (l) {
			strcat(out, " or ");
			l += 4;
		}
		l += scnprintf(out + l, sz - l, mem_lvl[i]);
	}
	if (*out == '\0')
		l += scnprintf(out, sz - l, "N/A");
	if (hit)
		l += scnprintf(out + l, sz - l, " hit");
	if (miss)
		l += scnprintf(out + l, sz - l, " miss");

	return l;
}

static const char * const snoop_access[] = {
	"N/A",
	"None",
	"Miss",
	"Hit",
	"HitM",
};

int perf_mem__snp_scnprintf(char *out, size_t sz, struct mem_info *mem_info)
{
	size_t i, l = 0;
	u64 m = PERF_MEM_SNOOP_NA;

	sz -= 1; /* -1 for null termination */
	out[0] = '\0';

	if (mem_info)
		m = mem_info->data_src.mem_snoop;

	for (i = 0; m && i < ARRAY_SIZE(snoop_access); i++, m >>= 1) {
		if (!(m & 0x1))
			continue;
		if (l) {
			strcat(out, " or ");
			l += 4;
		}
		l += scnprintf(out + l, sz - l, snoop_access[i]);
	}

	if (*out == '\0')
		l += scnprintf(out, sz - l, "N/A");

	return l;
}

int perf_mem__lck_scnprintf(char *out, size_t sz, struct mem_info *mem_info)
{
	u64 mask = PERF_MEM_LOCK_NA;
	int l;

	if (mem_info)
		mask = mem_info->data_src.mem_lock;

	if (mask & PERF_MEM_LOCK_NA)
		l = scnprintf(out, sz, "N/A");
	else if (mask & PERF_MEM_LOCK_LOCKED)
		l = scnprintf(out, sz, "Yes");
	else
		l = scnprintf(out, sz, "No");

	return l;
}

int perf_script__meminfo_scnprintf(char *out, size_t sz, struct mem_info *mem_info)
{
	int i = 0;

	i += perf_mem__lvl_scnprintf(out, sz, mem_info);
	i += scnprintf(out + i, sz - i, "|SNP ");
	i += perf_mem__snp_scnprintf(out + i, sz - i, mem_info);
	i += scnprintf(out + i, sz - i, "|TLB ");
	i += perf_mem__tlb_scnprintf(out + i, sz - i, mem_info);
	i += scnprintf(out + i, sz - i, "|LCK ");
	i += perf_mem__lck_scnprintf(out + i, sz - i, mem_info);

	return i;
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
