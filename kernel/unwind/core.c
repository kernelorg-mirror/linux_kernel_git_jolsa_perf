
#include <linux/module.h>
#include "internal.h"

extern const char __start___unwind_data[], __stop___unwind_data[];

static int __init unwind_init(void)
{
	printk("unwind start %p, stop %p\n", __start___unwind_data, __stop___unwind_data);
	return 0;
}

module_init(unwind_init);
