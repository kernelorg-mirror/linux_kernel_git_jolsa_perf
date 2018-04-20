#include <uapi/linux/bpf.h>
#include <bpf-helpers.h>

#define TASK_COMM_LEN 16
#define VAL_SIZE      128
#define ARG_CNT       6

char _license[] SEC("license") = "GPL";
int _version SEC("version") = LINUX_VERSION_CODE;

enum {
	EVENT_PROBE,
	EVENT_RET,
};

struct event_t {
	u16	probe;
	u16	type;
	u32	pid;
	char	val[VAL_SIZE];
};

struct bpf_map_def SEC("maps") __bpf_stdout__ = {
	.type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(u32),
	.max_entries = __NR_CPUS__,
};

SEC("execve=sys_execve filename argv")
int sys_execve_enter(void *ctx, char *filename, char **argv)
{
	struct data_t data;

#if 0
	const char *argp = NULL;

	data.pid = bpf_get_current_pid_tgid() >> 32;
	bpf_get_current_comm(&data.val, sizeof(data.val));

	bpf_perf_event_output(ctx, &__bpf_stdout__, bpf_get_smp_processor_id(),
			      &data, sizeof(data));

	data.type = EVENT_ARGV;
	bpf_probe_read(&argp, sizeof(argp), &argv[0]);

	if (argp) {
		bpf_probe_read(&data.val, sizeof(data.val), (void *) argp);

		bpf_perf_event_output(ctx, &__bpf_stdout__, bpf_get_smp_processor_id(),
				      &data, sizeof(data));
	}
#endif

	return 0;
}

SEC("execve=sys_execve%return")
int sys_execve_exit(void *ctx)
{
	return 0;
}

static void *hash;

int EVENT(void *ptr, u64 size)
{
#if 0
	struct data_t *data = ptr;

	if (data) {
		print("event %p, size %llu, type %d, ", ptr, size);
		print("pid %d, type %d\n", data->pid, data->type);

		if (data->type == EVENT_COMM)
			print(" comm %s\n", data->val);
		else if (data->type == EVENT_ARGV)
			print(" argv %s\n", data->val);
		else
			print("unknown type\n");
	}
#endif
	return 0;
}

static struct bpf_hash hash;

int BEGIN(void)
{
	return hash__init(&hash);
}

void END(void)
{
	print("END\n");
}
