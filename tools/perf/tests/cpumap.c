#include "tests.h"
#include "cpumap.h"

static int process_event(struct perf_tool *tool __maybe_unused,
			 union perf_event *event,
			 struct perf_sample *sample __maybe_unused,
			 struct machine *machine __maybe_unused)
{
	struct cpu_map_event *map = &event->cpu_map;

	TEST_ASSERT_VAL("wrong nr",   map->nr == 3);
	TEST_ASSERT_VAL("wrong cpu",  map->cpu[0] == 1);
	TEST_ASSERT_VAL("wrong cpu",  map->cpu[1] == 2);
	TEST_ASSERT_VAL("wrong cpu",  map->cpu[2] == 4);
	return 0;
}

int test__cpu_map_synthesize(void)
{
	struct cpu_map *cpus;

	cpus = cpu_map__new("1,2,4");


	TEST_ASSERT_VAL("failed to synthesize map",
		!perf_event__synthesize_cpu_map(NULL, cpus, process_event, NULL, PERF_STAT_CPU_MAP_TYPE__GLOBAL));

	return 0;
}
