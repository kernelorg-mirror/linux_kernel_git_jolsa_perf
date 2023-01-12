#include <linux/compiler.h>

static int noinline bpf_test_func(int a)
{
	return a + 1;
}

int bpf_test_func2_globl(int a)
{
	return bpf_test_func(a);
}
