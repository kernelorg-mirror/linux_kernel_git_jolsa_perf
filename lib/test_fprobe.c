// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * test_fprobe.c - simple sanity test for fprobe
 */

#include <linux/kernel.h>
#include <linux/fprobe.h>
#include <linux/random.h>
#include <kunit/test.h>

#define div_factor 3

static struct kunit *current_test;

static u32 rand1, entry_val, exit_val;

/* Use indirect calls to avoid inlining the target functions */
static u32 (*target)(u32 value);
static u32 (*target2)(u32 value);

static noinline u32 fprobe_target(u32 value)
{
	return (value / div_factor);
}

static noinline u32 fprobe_target2(u32 value)
{
	return (value / div_factor) + 1;
}

static notrace void fp_entry_handler(struct fprobe *fp, unsigned long ip, struct pt_regs *regs)
{
	KUNIT_EXPECT_FALSE(current_test, preemptible());
	/* This can be called on the fprobe_target and the fprobe_target2 */
	if (ip != (unsigned long)fprobe_target)
		KUNIT_EXPECT_EQ(current_test, ip, (unsigned long)fprobe_target2);
	entry_val = (rand1 / div_factor);
}

static notrace void fp_exit_handler(struct fprobe *fp, unsigned long ip, struct pt_regs *regs)
{
	unsigned long ret = regs_return_value(regs);

	KUNIT_EXPECT_FALSE(current_test, preemptible());
	if (ip != (unsigned long)fprobe_target) {
		KUNIT_EXPECT_EQ(current_test, ip, (unsigned long)fprobe_target2);
		KUNIT_EXPECT_EQ(current_test, ret, (rand1 / div_factor) + 1);
	} else
		KUNIT_EXPECT_EQ(current_test, ret, (rand1 / div_factor));
	KUNIT_EXPECT_EQ(current_test, entry_val, (rand1 / div_factor));
	exit_val = entry_val + div_factor;
}

static struct fprobe fp = {
	.entry_handler = fp_entry_handler,
	.exit_handler = fp_exit_handler,
};

static void test_fprobe(struct kunit *test)
{
	current_test = test;
	KUNIT_EXPECT_EQ(test, 0, register_fprobe(&fp, "fprobe_target*", NULL));

	entry_val = 0;
	exit_val = 0;
	target(rand1);
	KUNIT_EXPECT_NE(test, 0, entry_val);
	KUNIT_EXPECT_EQ(test, entry_val + div_factor, exit_val);

	entry_val = 0;
	exit_val = 0;
	target2(rand1);
	KUNIT_EXPECT_NE(test, 0, entry_val);
	KUNIT_EXPECT_EQ(test, entry_val + div_factor, exit_val);

	KUNIT_EXPECT_EQ(test, 0, unregister_fprobe(&fp));
}

static void test_fprobe_syms(struct kunit *test)
{
	const char *syms[] = {"fprobe_target", "fprobe_target2"};

	current_test = test;
	KUNIT_EXPECT_EQ(test, 0, register_fprobe_syms(&fp, syms, 2));

	entry_val = 0;
	exit_val = 0;
	target(rand1);
	KUNIT_EXPECT_NE(test, 0, entry_val);
	KUNIT_EXPECT_EQ(test, entry_val + div_factor, exit_val);

	entry_val = 0;
	exit_val = 0;
	target2(rand1);
	KUNIT_EXPECT_NE(test, 0, entry_val);
	KUNIT_EXPECT_EQ(test, entry_val + div_factor, exit_val);

	KUNIT_EXPECT_EQ(test, 0, unregister_fprobe(&fp));
}

static int fprobe_test_init(struct kunit *test)
{
	do {
		rand1 = prandom_u32();
	} while (rand1 <= div_factor);
	target = fprobe_target;
	target2 = fprobe_target2;
	return 0;
}

static struct kunit_case fprobe_testcases[] = {
	KUNIT_CASE(test_fprobe),
	KUNIT_CASE(test_fprobe_syms),
	{}
};

static struct kunit_suite fprobe_test_suite = {
	.name = "fprobe_test",
	.init = fprobe_test_init,
	.test_cases = fprobe_testcases,
};

kunit_test_suites(&fprobe_test_suite);

MODULE_LICENSE("GPL");
