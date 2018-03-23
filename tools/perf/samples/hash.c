#include <uapi/linux/bpf.h>
#include <bpf-helpers.h>

char _license[] SEC("license") = "GPL";
int _version SEC("version") = LINUX_VERSION_CODE;

SEC("raw_syscalls:sys_enter")
int func(void *ctx)
{
	return 0;
}

struct key_t {
	u32	 pid;
};

#define MAX_PATH 4096

struct value_t {
	char	str[MAX_PATH];
};


static void *hash;

int BEGIN(void)
{
	print("BEGIN");

	hash_init(&hash, sizeof(struct key_t), sizeof(struct value_t));
	return 0;
}

static struct value_t value = {
	.str = "",
};

void END(void)
{
	struct key_t   key;
	struct value_t *val;

	key.pid   = 123;
	strcat(value.str, "krava");

	if (hash_add(hash, &key, &value)) {
		print("hash: failed to add\n");
		return;
	}

	key.pid = 123;

	if (hash_lookup(hash, &key, (void **) &val)) {
		print("hash: failed to lookup\n");
		return;
	}

	print("hash 123 '%s'\n", val->str);

	key.pid   = 124;
	strcat(value.str, "krava");

	if (hash_add(hash, &key, &value)) {
		print("hash: failed to add\n");
		return;
	}

	key.pid = 123;

	if (hash_lookup(hash, &key, (void **) &val)) {
		print("hash: failed to lookup\n");
		return;
	}

	print("pid %d, val %s\n", key.pid, val->str);

	if (hash_remove(hash, &key)) {
		print("hash: failed to remove\n");
		return;
	}

	if (!hash_lookup(hash, &key, (void **) &val)) {
		print("hash: failed to not lookup\n");
		return;
	}

	hash_destroy(hash);

	print("END\n");
}
