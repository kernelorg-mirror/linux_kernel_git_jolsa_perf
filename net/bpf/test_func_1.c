#include <linux/compiler.h>
#include <linux/bpf.h>

static int noinline bpf_test_func(struct bpf_prog *prog)
{
	return prog->jited;
}

int bpf_test_func1_globl(struct bpf_prog *prog)
{
	return bpf_test_func(prog);
}
