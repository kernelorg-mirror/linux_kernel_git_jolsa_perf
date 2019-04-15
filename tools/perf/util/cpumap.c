// SPDX-License-Identifier: GPL-2.0
#include "util.h"
#include <api/fs/fs.h>
#include "../perf.h"
#include "cpumap.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/bitmap.h>
#include "asm/bug.h"

#include "sane_ctype.h"

static int max_cpu_num;
static int max_present_cpu_num;
static int max_node_num;
static int *cpunode_map;

static struct perf_cpu_map *cpu_map__from_entries(struct cpu_map_entries *cpus)
{
	struct perf_cpu_map *map;

	map = perf_perf_cpu_map__empty_new(cpus->nr);
	if (map) {
		unsigned i;

		for (i = 0; i < cpus->nr; i++) {
			/*
			 * Special treatment for -1, which is not real cpu number,
			 * and we need to use (int) -1 to initialize map[i],
			 * otherwise it would become 65535.
			 */
			if (cpus->cpu[i] == (u16) -1)
				perf_cpu_map__set(map, i, -1);
			else
				perf_cpu_map__set(map, i, (int) cpus->cpu[i]);
		}
	}

	return map;
}

static struct perf_cpu_map *cpu_map__from_mask(struct cpu_map_mask *mask)
{
	struct perf_cpu_map *map;
	int nr, nbits = mask->nr * mask->long_size * BITS_PER_BYTE;

	nr = bitmap_weight(mask->mask, nbits);

	map = perf_perf_cpu_map__empty_new(nr);
	if (map) {
		int cpu, i = 0;

		for_each_set_bit(cpu, mask->mask, nbits)
			perf_cpu_map__set(map, i++, cpu);
	}
	return map;

}

struct perf_cpu_map *cpu_map__new_data(struct cpu_map_data *data)
{
	if (data->type == PERF_CPU_MAP__CPUS)
		return cpu_map__from_entries((struct cpu_map_entries *)data->data);
	else
		return cpu_map__from_mask((struct cpu_map_mask *)data->data);
}

size_t cpu_map__fprintf(struct perf_cpu_map *map, FILE *fp)
{
#define BUFSIZE 1024
	char buf[BUFSIZE];

	perf_cpu_map__snprint(map, buf, sizeof(buf));
	return fprintf(fp, "%s\n", buf);
#undef BUFSIZE
}

static int cpu__get_topology_int(int cpu, const char *name, int *value)
{
	char path[PATH_MAX];

	snprintf(path, PATH_MAX,
		"devices/system/cpu/cpu%d/topology/%s", cpu, name);

	return sysfs__read_int(path, value);
}

int cpu_map__get_socket_id(int cpu)
{
	int value, ret = cpu__get_topology_int(cpu, "physical_package_id", &value);
	return ret ?: value;
}

int cpu_map__get_socket(struct perf_cpu_map *map, int idx, void *data __maybe_unused)
{
	int cpu;

	if (idx > perf_cpu_map__nr(map))
		return -1;

	cpu = perf_cpu_map__cpu(map, idx);

	return cpu_map__get_socket_id(cpu);
}

int cpu_map__get_die_id(int cpu)
{
	int value, ret = cpu__get_topology_int(cpu, "die_id", &value);

	return ret ?: value;
}

int cpu_map__get_die(struct perf_cpu_map *map, int idx, void *data)
{
	int cpu, die_id, s;

	if (idx > perf_cpu_map__nr(map))
		return -1;

	cpu = perf_cpu_map__cpu(map, idx);

	die_id = cpu_map__get_die_id(cpu);
	/* There is no die_id on legacy system. */
	if (die_id == -1)
		die_id = 0;

	s = cpu_map__get_socket(map, idx, data);
	if (s == -1)
		return -1;

	/*
	 * Encode socket in bit range 15:8
	 * die_id is relative to socket, and
	 * we need a global id. So we combine
	 * socket + die id
	 */
	if (WARN_ONCE(die_id >> 8, "The die id number is too big.\n"))
		return -1;

	if (WARN_ONCE(s >> 8, "The socket id number is too big.\n"))
		return -1;

	return (s << 8) | (die_id & 0xff);
}

int cpu_map__get_core_id(int cpu)
{
	int value, ret = cpu__get_topology_int(cpu, "core_id", &value);
	return ret ?: value;
}

int cpu_map__get_core(struct perf_cpu_map *map, int idx, void *data)
{
	int cpu, s_die;

	if (idx > perf_cpu_map__nr(map))
		return -1;

	cpu = perf_cpu_map__cpu(map, idx);

	cpu = cpu_map__get_core_id(cpu);

	/* s_die is the combination of socket + die id */
	s_die = cpu_map__get_die(map, idx, data);
	if (s_die == -1)
		return -1;

	/*
	 * encode socket in bit range 31:24
	 * encode die id in bit range 23:16
	 * core_id is relative to socket and die,
	 * we need a global id. So we combine
	 * socket + die id + core id
	 */
	if (WARN_ONCE(cpu >> 16, "The core id number is too big.\n"))
		return -1;

	return (s_die << 16) | (cpu & 0xffff);
}

int cpu_map__build_socket_map(struct perf_cpu_map *cpus, struct perf_cpu_map **sockp)
{
	return perf_cpu_map__build_map(cpus, sockp, cpu_map__get_socket, NULL);
}

int cpu_map__build_die_map(struct perf_cpu_map *cpus, struct perf_cpu_map **diep)
{
	return perf_cpu_map__build_map(cpus, diep, cpu_map__get_die, NULL);
}

int cpu_map__build_core_map(struct perf_cpu_map *cpus, struct perf_cpu_map **corep)
{
	return perf_cpu_map__build_map(cpus, corep, cpu_map__get_core, NULL);
}

/* setup simple routines to easily access node numbers given a cpu number */
static int get_max_num(char *path, int *max)
{
	size_t num;
	char *buf;
	int err = 0;

	if (filename__read_str(path, &buf, &num))
		return -1;

	buf[num] = '\0';

	/* start on the right, to find highest node num */
	while (--num) {
		if ((buf[num] == ',') || (buf[num] == '-')) {
			num++;
			break;
		}
	}
	if (sscanf(&buf[num], "%d", max) < 1) {
		err = -1;
		goto out;
	}

	/* convert from 0-based to 1-based */
	(*max)++;

out:
	free(buf);
	return err;
}

/* Determine highest possible cpu in the system for sparse allocation */
static void set_max_cpu_num(void)
{
	const char *mnt;
	char path[PATH_MAX];
	int ret = -1;

	/* set up default */
	max_cpu_num = 4096;
	max_present_cpu_num = 4096;

	mnt = sysfs__mountpoint();
	if (!mnt)
		goto out;

	/* get the highest possible cpu number for a sparse allocation */
	ret = snprintf(path, PATH_MAX, "%s/devices/system/cpu/possible", mnt);
	if (ret == PATH_MAX) {
		pr_err("sysfs path crossed PATH_MAX(%d) size\n", PATH_MAX);
		goto out;
	}

	ret = get_max_num(path, &max_cpu_num);
	if (ret)
		goto out;

	/* get the highest present cpu number for a sparse allocation */
	ret = snprintf(path, PATH_MAX, "%s/devices/system/cpu/present", mnt);
	if (ret == PATH_MAX) {
		pr_err("sysfs path crossed PATH_MAX(%d) size\n", PATH_MAX);
		goto out;
	}

	ret = get_max_num(path, &max_present_cpu_num);

out:
	if (ret)
		pr_err("Failed to read max cpus, using default of %d\n", max_cpu_num);
}

/* Determine highest possible node in the system for sparse allocation */
static void set_max_node_num(void)
{
	const char *mnt;
	char path[PATH_MAX];
	int ret = -1;

	/* set up default */
	max_node_num = 8;

	mnt = sysfs__mountpoint();
	if (!mnt)
		goto out;

	/* get the highest possible cpu number for a sparse allocation */
	ret = snprintf(path, PATH_MAX, "%s/devices/system/node/possible", mnt);
	if (ret == PATH_MAX) {
		pr_err("sysfs path crossed PATH_MAX(%d) size\n", PATH_MAX);
		goto out;
	}

	ret = get_max_num(path, &max_node_num);

out:
	if (ret)
		pr_err("Failed to read max nodes, using default of %d\n", max_node_num);
}

int cpu__max_node(void)
{
	if (unlikely(!max_node_num))
		set_max_node_num();

	return max_node_num;
}

int cpu__max_cpu(void)
{
	if (unlikely(!max_cpu_num))
		set_max_cpu_num();

	return max_cpu_num;
}

int cpu__max_present_cpu(void)
{
	if (unlikely(!max_present_cpu_num))
		set_max_cpu_num();

	return max_present_cpu_num;
}


int cpu__get_node(int cpu)
{
	if (unlikely(cpunode_map == NULL)) {
		pr_debug("cpu_map not initialized\n");
		return -1;
	}

	return cpunode_map[cpu];
}

static int init_cpunode_map(void)
{
	int i;

	set_max_cpu_num();
	set_max_node_num();

	cpunode_map = calloc(max_cpu_num, sizeof(int));
	if (!cpunode_map) {
		pr_err("%s: calloc failed\n", __func__);
		return -1;
	}

	for (i = 0; i < max_cpu_num; i++)
		cpunode_map[i] = -1;

	return 0;
}

int cpu__setup_cpunode_map(void)
{
	struct dirent *dent1, *dent2;
	DIR *dir1, *dir2;
	unsigned int cpu, mem;
	char buf[PATH_MAX];
	char path[PATH_MAX];
	const char *mnt;
	int n;

	/* initialize globals */
	if (init_cpunode_map())
		return -1;

	mnt = sysfs__mountpoint();
	if (!mnt)
		return 0;

	n = snprintf(path, PATH_MAX, "%s/devices/system/node", mnt);
	if (n == PATH_MAX) {
		pr_err("sysfs path crossed PATH_MAX(%d) size\n", PATH_MAX);
		return -1;
	}

	dir1 = opendir(path);
	if (!dir1)
		return 0;

	/* walk tree and setup map */
	while ((dent1 = readdir(dir1)) != NULL) {
		if (dent1->d_type != DT_DIR || sscanf(dent1->d_name, "node%u", &mem) < 1)
			continue;

		n = snprintf(buf, PATH_MAX, "%s/%s", path, dent1->d_name);
		if (n == PATH_MAX) {
			pr_err("sysfs path crossed PATH_MAX(%d) size\n", PATH_MAX);
			continue;
		}

		dir2 = opendir(buf);
		if (!dir2)
			continue;
		while ((dent2 = readdir(dir2)) != NULL) {
			if (dent2->d_type != DT_LNK || sscanf(dent2->d_name, "cpu%u", &cpu) < 1)
				continue;
			cpunode_map[cpu] = mem;
		}
		closedir(dir2);
	}
	closedir(dir1);
	return 0;
}

const struct perf_cpu_map *cpu_map__online(void) /* thread unsafe */
{
	static const struct perf_cpu_map *online = NULL;

	if (!online)
		online = perf_cpu_map__new(NULL); /* from /sys/devices/system/cpu/online */

	return online;
}
