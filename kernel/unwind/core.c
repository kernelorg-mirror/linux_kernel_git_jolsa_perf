#include <linux/module.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include "internal.h"

extern const char __start___unwind_data[], __stop___unwind_data[];

BPF_CALL_1(bpf_unwind, void *, func)
{
	printk("bpf_unwind %p\n", func);
	return 0;
}

const struct bpf_func_proto bpf_unwind_proto = {
	.func		= bpf_unwind,
	.gpl_only	= false,
	.pkt_access	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_ANYTHING,
};

static const struct bpf_func_proto *
unwind_func_proto(enum bpf_func_id func_id)
{
	switch (func_id) {
	case BPF_FUNC_unwind:
		return &bpf_unwind_proto;
	default:
		return NULL;
	}
}

static const struct bpf_verifier_ops unwind_type_ops = {
	.get_func_proto		= unwind_func_proto,
};

static struct bpf_prog_type_list unwind_type __read_mostly = {
	.ops	= &unwind_type_ops,
	.type	= BPF_PROG_TYPE_UNWIND,
};

static int __init unwind_init(void)
{
	bpf_register_prog_type(&unwind_type);
	return 0;
}

module_init(unwind_init);
