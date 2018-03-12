#include <uapi/linux/bpf.h>
#include <bpf-helpers.h>

#define TASK_COMM_LEN 16

char _license[] SEC("license") = "GPL";
int _version SEC("version") = LINUX_VERSION_CODE;

struct key_t {
	char comm[TASK_COMM_LEN];
};

struct bpf_map_def SEC("maps") counts_map = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(struct key_t),
	.value_size = sizeof(u64),
	.max_entries = 100,
};

struct bpf_map_def SEC("maps") __bpf_stdout__ = {
	.type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(u32),
	.max_entries = __NR_CPUS__,
};

SEC("raw_syscalls:sys_enter")
int func(void *ctx)
{
	u64 *val, one = 1;
	struct key_t key;
	char comm[TASK_COMM_LEN];
	char output_str[] = "Raise a BPF event!";

	bpf_get_current_comm(&key.comm, sizeof(comm));

	val = bpf_map_lookup_elem(&counts_map, &key);
	if (val)
		(*val)++;
	else
		bpf_map_update_elem(&counts_map, &key, &one, BPF_NOEXIST);

	bpf_perf_event_output(ctx, &__bpf_stdout__, bpf_get_smp_processor_id(),
			      &output_str, sizeof(output_str));
	return 0;
}

static void krava(void)
{
	struct key_t key = {}, next_key;
	u64 value;
	int i = 0;

	while (map_get_next_key(&counts_map, &key, &next_key) == 0) {
		if (map_lookup_elem(&counts_map, &next_key, &value))
			continue;

		print("%18s %16lu\n", next_key.comm, value);
		key = next_key;
	}
}

int EVENT(void *ptr, u64 size)
{
	print("event %p, size %llu, %s\n", ptr, size, ptr);
	return 0;
}

int TIMER(void)
{
	print("timer\n");
	return 0;
}

int BEGIN(void)
{
	set_timer(1);
	print("BEGIN\n");
	return 0;
}

void END(void)
{
	u64 value;
	int i = 0;

	print("END\n");
	print("%18s %16s\n", "comm","value");

	krava();
}
