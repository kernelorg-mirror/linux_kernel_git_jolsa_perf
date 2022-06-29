// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>

#include <linux/mm.h> /* for handle_mm_fault() */
#include <linux/ftrace.h>
#include <linux/sched/stat.h>
#include <asm/asm-offsets.h>

#ifdef CONFIG_X86_64

#include <asm/ibt.h>

#define TRAMP(__name)					\
extern void __name(void *);				\
extern void __name ## _func(unsigned long ip);		\
unsigned int __name ## _cnt;				\
void __name ## _func(unsigned long ip)			\
{							\
	__name ## _cnt++;				\
	printk("  called: " #__name " <- %ps\n", (void *) ip); \
}							\
asm (							\
"	.pushsection    .text, \"ax\", @progbits\n"	\
"	.type		" #__name ", @function\n"	\
"	.globl		" #__name "\n"			\
"   " #__name ":"					\
	ASM_ENDBR					\
"	pushq %rbp\n"					\
"	movq %rsp, %rbp\n"				\
"	pushq %rdi\n"					\
"	movq 8(%rbp), %rdi\n"				\
"	call " #__name "_func\n"			\
"	popq %rdi\n"					\
"	leave\n"					\
	ASM_RET						\
"	.size		" #__name ", .-" #__name "\n"	\
"	.popsection\n"					\
);

TRAMP(tramp_0)
TRAMP(tramp_1)
TRAMP(tramp_2)
TRAMP(tramp_3)
TRAMP(tramp_4)
TRAMP(tramp_5)
TRAMP(tramp_6)
TRAMP(tramp_7)
TRAMP(tramp_8)
TRAMP(tramp_9)

#endif /* CONFIG_X86_64 */

static struct ftrace_hash *hash;

static void hash_printk(void)
{
	struct ftrace_func_entry *entry;
	int i, size;

	size = 1 << hash->size_bits;
	for (i = 0; i < size; i++) {
		hlist_for_each_entry(entry, &hash->buckets[i], hlist)
			printk("%ps -> %ps\n", (void*) entry->ip, (void*) entry->direct);
	}
}

static void hash_set(void *ip, void *direct)
{
	struct ftrace_func_entry *entry;

	entry = ftrace_lookup_ip(hash, (unsigned long) ip);
	if (entry) {
		if (direct)
			entry->direct = (unsigned long) direct;
		else
			ftrace_hash_free_entry(hash, entry);
		return;
	}

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (WARN_ON(!entry))
		return;

	entry->ip = (unsigned long) ip;
	entry->direct = (unsigned long) direct;
	ftrace_hash_add_entry(hash, entry);
}

static void runall(const char *str)
{
	tramp_0_cnt = 0;
	tramp_1_cnt = 0;
	tramp_2_cnt = 0;
	tramp_3_cnt = 0;
	tramp_4_cnt = 0;
	tramp_5_cnt = 0;
	tramp_6_cnt = 0;
	tramp_7_cnt = 0;
	tramp_8_cnt = 0;
	tramp_9_cnt = 0;

	printk("\nrunning '%s':\n", str);

	ftrace_test_0();
	ftrace_test_1();
	ftrace_test_2();
	ftrace_test_3();
	ftrace_test_4();
	ftrace_test_5();
	ftrace_test_6();
	ftrace_test_7();
	ftrace_test_8();
	ftrace_test_9();

	printk("  result: %d%d%d%d%d%d%d%d%d%d\n\n",
		tramp_0_cnt,
		tramp_1_cnt,
		tramp_2_cnt,
		tramp_3_cnt,
		tramp_4_cnt,
		tramp_5_cnt,
		tramp_6_cnt,
		tramp_7_cnt,
		tramp_8_cnt,
		tramp_9_cnt);
}

static int __init ftrace_direct_set_init(void)
{
	int err;

	hash = ftrace_hash_alloc(FTRACE_HASH_DEFAULT_BITS);
	if (!hash)
		return -ENOMEM;

	hash_set(ftrace_test_0, tramp_0);
	hash_set(ftrace_test_1, tramp_1);
	hash_printk();

	err = set_ftrace_direct(hash);
	if (err)
		goto out_free;

	runall("tramp_0|1");

	hash_set(ftrace_test_0, tramp_2);
	hash_set(ftrace_test_1, tramp_3);
	hash_printk();

	err = set_ftrace_direct(hash);
	if (err)
		goto out_free;

	runall("tramp_2|3");

	hash_set(ftrace_test_0, tramp_0);
	hash_set(ftrace_test_1, tramp_0);
	hash_set(ftrace_test_2, tramp_0);
	hash_set(ftrace_test_3, tramp_0);
	hash_set(ftrace_test_4, tramp_4);
	hash_set(ftrace_test_5, tramp_5);
	hash_set(ftrace_test_6, tramp_6);
	hash_set(ftrace_test_7, tramp_7);
	hash_set(ftrace_test_8, tramp_8);
	hash_set(ftrace_test_9, tramp_9);
	hash_printk();

	err = set_ftrace_direct(hash);
	if (err)
		goto out_free;

	runall("tramp_0(4)|4|5|6|7|8|9");

	hash_set(ftrace_test_0, tramp_1);
	hash_set(ftrace_test_1, tramp_1);
	hash_set(ftrace_test_2, tramp_1);
	hash_set(ftrace_test_3, tramp_2);
	hash_set(ftrace_test_4, tramp_2);
	hash_set(ftrace_test_5, tramp_2);
	hash_set(ftrace_test_6, tramp_3);
	hash_set(ftrace_test_7, tramp_3);
	hash_set(ftrace_test_8, tramp_3);
	hash_set(ftrace_test_9, tramp_4);
	hash_printk();

	err = set_ftrace_direct(hash);
	if (err)
		goto out_free;

	runall("tramp_1(3)|2(3)|3(3)|4");
	return 0;

out_free:
	ftrace_hash_free(hash);
	return -ENOMEM;
}

static void __exit ftrace_direct_set_exit(void)
{
	struct ftrace_func_entry *entry;
	int i, size;

	size = 1 << hash->size_bits;
	for (i = 0; i < size; i++) {
		hlist_for_each_entry(entry, &hash->buckets[i], hlist)
			entry->direct = 0;
	}

	WARN_ON(set_ftrace_direct(hash));
	ftrace_hash_free(hash);
}

module_init(ftrace_direct_set_init);
module_exit(ftrace_direct_set_exit);

MODULE_AUTHOR("Jiri Olsa");
MODULE_DESCRIPTION("Example use case of using register_ftrace_direct_set()");
MODULE_LICENSE("GPL");
