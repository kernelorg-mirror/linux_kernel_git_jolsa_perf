#include "tests.h"
#include "cpumap.h"

static int process_event_mask(struct perf_tool *tool __maybe_unused,
			 union perf_event *event,
			 struct perf_sample *sample __maybe_unused,
			 struct machine *machine __maybe_unused)
{
	struct cpu_map_event *map_event = &event->cpu_map;
	struct cpu_map_data_mask *mask;
	struct cpu_map_data *data;
	struct cpu_map *map;

	data = &map_event->data;

	TEST_ASSERT_VAL("wrong type", data->type == PERF_CPU_MAP__MASK);

	mask = (struct cpu_map_data_mask *) data->data;

	TEST_ASSERT_VAL("wrong nr",   mask->nr == 1);
	TEST_ASSERT_VAL("wrong cpu",  test_bit(1, mask->mask));
	TEST_ASSERT_VAL("wrong cpu",  test_bit(2, mask->mask));
	TEST_ASSERT_VAL("wrong cpu",  test_bit(3, mask->mask));
	TEST_ASSERT_VAL("wrong cpu",  test_bit(4, mask->mask));
	TEST_ASSERT_VAL("wrong cpu",  test_bit(5, mask->mask));
	TEST_ASSERT_VAL("wrong cpu",  test_bit(6, mask->mask));

	map = cpu_map__new_data(data);
	TEST_ASSERT_VAL("wrong nr",  map->nr == 6);
	TEST_ASSERT_VAL("wrong cpu", map->map[0] == 1);
	TEST_ASSERT_VAL("wrong cpu", map->map[1] == 2);
	TEST_ASSERT_VAL("wrong cpu", map->map[2] == 3);
	TEST_ASSERT_VAL("wrong cpu", map->map[3] == 4);
	TEST_ASSERT_VAL("wrong cpu", map->map[4] == 5);
	TEST_ASSERT_VAL("wrong cpu", map->map[5] == 6);
	TEST_ASSERT_VAL("wrong refcnt", atomic_read(&map->refcnt) == 1);
	cpu_map__put(map);
	return 0;
}

static int process_event_cpus(struct perf_tool *tool __maybe_unused,
			 union perf_event *event,
			 struct perf_sample *sample __maybe_unused,
			 struct machine *machine __maybe_unused)
{
	struct cpu_map_event *map_event = &event->cpu_map;
	struct cpu_map_data_cpus *cpus;
	struct cpu_map_data *data;
	struct cpu_map *map;

	data = &map_event->data;

	TEST_ASSERT_VAL("wrong type", data->type == PERF_CPU_MAP__CPUS);

	cpus = (struct cpu_map_data_cpus *) data->data;

	TEST_ASSERT_VAL("wrong nr",   cpus->nr == 2);
	TEST_ASSERT_VAL("wrong cpu",  cpus->cpu[0] == 1);
	TEST_ASSERT_VAL("wrong cpu",  cpus->cpu[1] == 256);

	map = cpu_map__new_data(data);
	TEST_ASSERT_VAL("wrong nr",  map->nr == 2);
	TEST_ASSERT_VAL("wrong cpu", map->map[0] == 1);
	TEST_ASSERT_VAL("wrong cpu", map->map[1] == 256);
	TEST_ASSERT_VAL("wrong refcnt", atomic_read(&map->refcnt) == 1);
	cpu_map__put(map);
	return 0;
}


int test__cpu_map_synthesize(void)
{
	struct cpu_map *cpus;

	/* This one is better stores in mask. */
	cpus = cpu_map__new("1,2,3,4,5,6");

	TEST_ASSERT_VAL("failed to synthesize map",
		!perf_event__synthesize_cpu_map(NULL, cpus, process_event_mask, NULL));

	cpu_map__put(cpus);

	/* This one is better stores in cpu values. */
	cpus = cpu_map__new("1,256");

	TEST_ASSERT_VAL("failed to synthesize map",
		!perf_event__synthesize_cpu_map(NULL, cpus, process_event_cpus, NULL));

	cpu_map__put(cpus);
	return 0;
}
