#include <stdio.h>

#define ___PASTE(a, b) a##b
#define __PASTE(a, b) ___PASTE(a, b)

#define __NAME(prefix) \
       __PASTE(prefix, __COUNTER__)

#define FUNC \
static __attribute__((noinline)) __attribute__((unused)) int __NAME(uprobe_multi_func_)(void) { return 0; }

#define FUNC10    FUNC FUNC FUNC FUNC FUNC FUNC FUNC FUNC FUNC FUNC
#define FUNC100   FUNC10 FUNC10 FUNC10 FUNC10 FUNC10 FUNC10 FUNC10 FUNC10 FUNC10 FUNC10
#define FUNC1000  FUNC100 FUNC100 FUNC100 FUNC100 FUNC100 FUNC100 FUNC100 FUNC100 FUNC100 FUNC100
#define FUNC10000 FUNC1000 FUNC1000 FUNC1000 FUNC1000 FUNC1000 FUNC1000 FUNC1000 FUNC1000 FUNC1000 FUNC1000

FUNC10000
FUNC10000
FUNC10000
FUNC10000
FUNC10000

int main(void)
{
	uprobe_multi_func_1();
	uprobe_multi_func_2();
	uprobe_multi_func_3();
	uprobe_multi_func_4();
	uprobe_multi_func_5();
	uprobe_multi_func_6();
	uprobe_multi_func_7();
	uprobe_multi_func_8();
	uprobe_multi_func_9();
	return 0;
}
