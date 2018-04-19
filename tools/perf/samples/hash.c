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
static struct value_t value;

int BEGIN(void)
{
	print("BEGIN");

	hash_init((void *) &hash, sizeof(struct key_t), sizeof(struct value_t));
	print("hash3 %p\n", hash);
#if 0


	print("hash1 %p %p\n", hash, &hash);

	hash_init((void *) &hash, sizeof(struct key_t), sizeof(struct value_t));

	print("hash2 %p\n", hash);
#endif
	return 0;
}


#if 0
void END(void)
{
	struct key_t   key;
	struct value_t *val;

	print("hash3 %p %p\n", hash, &value.str);

	key.pid   = 123;
	concat(value.str, "krava");

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
#endif
