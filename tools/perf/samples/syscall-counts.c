#include <uapi/linux/bpf.h>
#include <bpf-helpers.h>
#include <bpf-userfuncs.h>

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

SEC("raw_syscalls:sys_enter")
int func(void *ctx)
{
	u64 *val, one = 1;
	struct key_t key;
	char comm[TASK_COMM_LEN];

	bpf_get_current_comm(&key.comm, sizeof(comm));

	val = bpf_map_lookup_elem(&counts_map, &key);
	if (val)
		(*val)++;
	else
		bpf_map_update_elem(&counts_map, &key, &one, BPF_NOEXIST);

	return 0;
}

int BEGIN(void)
{
	print("BEGIN\n");
	return 0;
}

void END(void)
{
	struct key_t key = {}, next_key;
	u64 value;
	int i = 0;

	print("END\n");
	print("\n              comm            value\n");

	while (bpfu_map_get_next_key(&counts_map, &key, &next_key) == 0) {
                if (bpfu_map_lookup_elem(&counts_map, &next_key, &value))
			continue;

		print("%18s %16lu\n", next_key.comm, value);
                key = next_key;
        }
}
