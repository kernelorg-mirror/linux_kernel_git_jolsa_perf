#include <linux/debugfs.h>
#include <linux/perf_event.h>

static struct perf_event_attr attr = {
	.type   = PERF_TYPE_HARDWARE,
	.config = PERF_COUNT_HW_CPU_CYCLES,
/* XXX pick
	PERF_COUNT_HW_CPU_CYCLES		= 0,
	PERF_COUNT_HW_INSTRUCTIONS		= 1,
	PERF_COUNT_HW_CACHE_REFERENCES		= 2,
	PERF_COUNT_HW_CACHE_MISSES		= 3,
	PERF_COUNT_HW_BRANCH_INSTRUCTIONS	= 4,
	PERF_COUNT_HW_BRANCH_MISSES		= 5,
	PERF_COUNT_HW_BUS_CYCLES		= 6,
	PERF_COUNT_HW_STALLED_CYCLES_FRONTEND	= 7,
	PERF_COUNT_HW_STALLED_CYCLES_BACKEND	= 8,
	PERF_COUNT_HW_REF_CPU_CYCLES		= 9,
*/
	.size = sizeof(struct perf_event_attr),
};

static u64 val;

static int pita_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t pita_read(struct file *filp, char __user *ubuf,
			 size_t cnt, loff_t *ppos)
{
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%llu\n", val);
        return simple_read_from_buffer(ubuf, cnt, ppos, buf, len);
}

static ssize_t pita_write(struct file *filp, const char __user *ubuf,
			  size_t cnt, loff_t *ppos)
{
	struct perf_event *event;
	unsigned int counter;

	event = perf_event_create_kernel_counter(&attr, -1, current, NULL, NULL);
	if (!event) {
		printk("failed to create event\n");
		return -EINVAL;
	}

	counter = event->hw.idx;
	if (event->hw.idx >= INTEL_PMC_IDX_FIXED) {
		counter -= INTEL_PMC_IDX_FIXED;
		counter |= 1 << 30;
	}

	local_irq_disable();

	val = native_read_pmc(counter);

	asm("nop");
	asm("nop");
	asm("nop");

	val = native_read_pmc(counter) - val;

	local_irq_enable();

	perf_event_release_kernel(event);
	return cnt;
}

static const struct file_operations pita_fops = {
	.open		= pita_open,
	.read		= pita_read,
	.write		= pita_write,
	.llseek		= default_llseek,
};

static int __init pita_init(void)
{
	WARN_ON(!debugfs_create_file("pita", 0644, NULL, NULL, &pita_fops));
	printk("pita initialized\n");
	return 0;
}

device_initcall(pita_init);
