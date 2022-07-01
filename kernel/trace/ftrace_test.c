#include <linux/export.h>

#define FUNC(__name)		\
int __name(void) { return 0; }	\
EXPORT_SYMBOL_GPL(__name);

FUNC(ftrace_test_0)
FUNC(ftrace_test_1)
FUNC(ftrace_test_2)
FUNC(ftrace_test_3)
FUNC(ftrace_test_4)
FUNC(ftrace_test_5)
FUNC(ftrace_test_6)
FUNC(ftrace_test_7)
FUNC(ftrace_test_8)
FUNC(ftrace_test_9)
