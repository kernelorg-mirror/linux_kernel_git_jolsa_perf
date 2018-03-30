#include <uapi/linux/bpf.h>
#include <generated/uapi/linux/buildid.h>

#define SEC(NAME) __attribute__((section(NAME), used))

char _license[] SEC("license") = "GPL";
int _version    SEC("version") = LINUX_VERSION_CODE;
char _buildid[] SEC("buildid") = LINUX_BUILDID_DATA;

static unsigned long long (*bpf_get_smp_processor_id)(void) =
	(void *) BPF_FUNC_get_smp_processor_id;
static int (*bpf_perf_event_output)(void *ctx, void *map,
                                    unsigned long long flags, void *data,
                                    int size) =
        (void *) BPF_FUNC_perf_event_output;

struct bpf_map_def {
        unsigned int type;
        unsigned int key_size;
        unsigned int value_size;
        unsigned int max_entries;
        unsigned int map_flags;
        unsigned int inner_map_idx;
        unsigned int numa_node;
};

struct bpf_map_def SEC("maps") __bpf_stdout__ = {
	.type		= BPF_MAP_TYPE_PERF_EVENT_ARRAY,
	.key_size	= sizeof(int),
	.value_size	= sizeof(u32),
	.max_entries	= __NR_CPUS__,
};

SEC("probe=sys_read")
int func(void *ctx)
{
	char output_str[] = "Raise a BPF event!";

	bpf_perf_event_output(ctx, &__bpf_stdout__, bpf_get_smp_processor_id(),
			      &output_str, sizeof(output_str));
	return 0;
}
