#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/compiler.h>
#include <api/fs/fs.h>
#include "topology.h"
#include "util.h"

static int read_int(const char *dir, const char *file, int *value)
{
	char path[PATH_MAX];

	scnprintf(path, PATH_MAX, "%s/%s", dir, file);
	return filename__read_int(path, value);
}

static int read_str(const char *dir, const char *file, char **str)
{
	char path[PATH_MAX];
	size_t size;
	int ret;

	scnprintf(path, PATH_MAX, "%s/%s", dir, file);
	ret = filename__read_str(path, str, &size);
	if (!ret) {
		char *nl = strchr(*str, '\n');
		if (nl)
			*nl = 0;
	}
	return ret;
}

static struct perf_tp_cpu *cpu_new(struct perf_tp *tp, int id)
{
	struct perf_tp_cpu *cpu;
	size_t size = (tp->num_cpus + 1) * sizeof(*cpu);

	tp->cpus = realloc(tp->cpus, size);
	if (!tp->cpus)
		return NULL;

	cpu = tp->cpus + tp->num_cpus;
	tp->num_cpus++;

	memset(cpu, 0x0, sizeof(*cpu));
	cpu->id = id;
	return cpu;
}

static struct perf_tp_cache *cache_new(struct perf_tp_cpu *cpu, int id)
{
	struct perf_tp_cache *cache;
	size_t size = (cpu->num_caches + 1) * sizeof(*cache);

	cpu->caches = realloc(cpu->caches, size);
	if (!cpu->caches)
		return NULL;

	cache = cpu->caches + cpu->num_caches;
	cpu->num_caches++;

	memset(cache, 0x0, sizeof(*cache));
	cache->id = id;
	return cache;
}

static int cpu_init_topology(struct perf_tp_cpu *cpu, const char *dir)
{
	char path[PATH_MAX];

	scnprintf(path, PATH_MAX, "%s/cpu%d/topology", dir, cpu->id);

	if (read_int(path, "core_id",		  &cpu->id_core) ||
	    read_int(path, "physical_package_id", &cpu->id_socket))
		return -1;

	return 0;
}

static int cache_init(struct perf_tp_cache *cache, const char *dir)
{
	char path[PATH_MAX];

	scnprintf(path, PATH_MAX, "%s/index%d/", dir, cache->id);

	if (read_int(path, "level",		    &cache->level) ||
	    read_int(path, "number_of_sets",	    &cache->number_of_sets) ||
	    read_int(path, "ways_of_associativity", &cache->ways_of_associativity))
		return -1;


	if (read_str(path, "type", &cache->type) ||
	    read_str(path, "size", &cache->size))
		return -1;

	return 0;
}

static int cache_add(struct perf_tp_cpu *cpu, const char *dir, int id)
{
	struct perf_tp_cache *cache;

	cache = cache_new(cpu, id);
	if (!cache)
		return -ENOMEM;

	return cache_init(cache, dir);
}

static int cache_cb(const char *dir, char *file, void *data)
{
	struct perf_tp_cpu *cpu = data;
	int n, id;

	n = sscanf(file, "index%d", &id);
	if (n != 1)
		return 0;

	return cache_add(cpu, dir, id);
}

static int cpu_init_cache(struct perf_tp_cpu *cpu, const char *dir)
{
	char path[PATH_MAX];

	scnprintf(path, PATH_MAX, "%s/cpu%d/cache", dir, cpu->id);
	return iter_dir(path, cache_cb, cpu);
}

static int cpu_init(struct perf_tp_cpu *cpu, const char *dir)
{
	return cpu_init_topology(cpu, dir) ||
	       cpu_init_cache(cpu, dir);
}

static int cpu_add(struct perf_tp *tp, const char *dir, int id)
{
	struct perf_tp_cpu *cpu;

	cpu = cpu_new(tp, id);
	if (!cpu)
		return -ENOMEM;

	return cpu_init(cpu, dir);
}

static int cpu_cb(const char *dir, char *file, void *data)
{
	struct perf_tp *tp = data;
	int n, id;

	n = sscanf(file, "cpu%d", &id);
	if (n != 1)
		return 0;

	return cpu_add(tp, dir, id);
}

static int iterate_cpus(void *data)
{
	const char *mnt;
	char path[PATH_MAX];

	mnt = sysfs__mountpoint();
	if (!mnt)
		return -1;

	snprintf(path, PATH_MAX, "%s/devices/system/cpu/", mnt);
	return iter_dir(path, cpu_cb, data);
}

static int comp_cache(const void *a, const void *b)
{
	struct perf_tp_cache *cache_a = (struct perf_tp_cache *) a;
	struct perf_tp_cache *cache_b = (struct perf_tp_cache *) b;

	return cache_a->level > cache_b->level;
}

static int comp_cpu(const void *a, const void *b)
{
	struct perf_tp_cpu *cpu_a = (struct perf_tp_cpu *) a;
	struct perf_tp_cpu *cpu_b = (struct perf_tp_cpu *) b;

	return cpu_a->id > cpu_b->id;
}

static int sort_cpus(struct perf_tp *tp)
{
	int i;

	for (i = 0; i < tp->num_cpus; i++) {
		struct perf_tp_cpu *cpu = &tp->cpus[i];

		qsort(cpu->caches, cpu->num_caches, sizeof(struct perf_tp_cache), comp_cache);
	}

	qsort(tp->cpus, tp->num_cpus, sizeof(struct perf_tp_cpu), comp_cpu);
	return 0;
}

int perf_tp__init(struct perf_tp *tp)
{
	int ret;

	memset(tp, 0, sizeof(*tp));
	ret = iterate_cpus(tp);
	if (!ret)
		sort_cpus(tp);
	return ret;
}

void perf_tp__clean(struct perf_tp *tp)
{
	int i,j;

	for (i = 0; i < tp->num_cpus; i++) {
		struct perf_tp_cpu *cpu = &tp->cpus[i];

		for (j = 0; j < cpu->num_caches; j++) {
			struct perf_tp_cache *cache = &cpu->caches[j];

			free(cache->type);
			free(cache->size);
		}
		free(cpu->caches);
	}

	free(tp->cpus);
}

int perf_tp__fprintf(FILE *file, struct perf_tp *tp)
{
	int i;

	fprintf(file, "%3s %4s %6s LEVEL-TYPE-SIZE[SETSxASSOC]\n", "CPU", "CORE", "SOCKET");

	for (i = 0; i < tp->num_cpus; i++) {
		struct perf_tp_cpu *cpu = &tp->cpus[i];
		int j;

		fprintf(file, "%3d %4d %6d ", cpu->id, cpu->id_core, cpu->id_socket);

		for (j = 0; j < cpu->num_caches; j++) {
			struct perf_tp_cache *cache = &cpu->caches[j];

			fprintf(file, "L%d-%s-%s[%dx%d] ",
				cache->level, cache->type, cache->size,
				cache->number_of_sets, cache->ways_of_associativity);
		}

		fprintf(file, "\n");
	}

	return 0;
}
