#include <linux/debugfs.h>
#include <linux/perf_event.h>

static struct dentry *d_pita;
static struct dentry *d_test;
static bool initialized;

static struct perf_event_attr attr = {
	.size = sizeof(struct perf_event_attr),
};

static int test_open(struct inode *inode, struct file *filp)
{
	return initialized ? 0 : -EINVAL;
}

static ssize_t test_read(struct file *filp, char __user *ubuf,
			 size_t cnt, loff_t *ppos)
{
	return 0;
}

static void overflow(struct perf_event *event, struct perf_sample_data *data,
		     struct pt_regs *regs)
{
}

static ssize_t test_write(struct file *filp, const char __user *ubuf,
			  size_t cnt, loff_t *ppos)
{
	struct perf_event *event;
	struct perf_sample_data data;

	event = perf_event_create_kernel_counter(&attr, -1, current,
                                 overflow, NULL);

	perf_sample_data_init(&data, 0, 100);
	perf_event_overflow(event, &data, NULL);
	perf_event_release_kernel(event);
	return 0;
}

static const struct file_operations test_fops = {
	.open		= test_open,
	.read		= test_read,
	.write		= test_write,
	.llseek		= default_llseek,
};

static int pita_pmu_init(struct perf_event *event)
{
	return 0;
}

static struct pmu pita_pmu = {
	.task_ctx_nr	= perf_sw_context,
	.event_init	= pita_pmu_init,
};

__init void pita_init(void)
{
	d_pita = debugfs_create_dir("pita", NULL);
	if (WARN_ON(!d_pita))
		return;

	d_test = debugfs_create_file("test", 0644, d_pita, NULL, &test_fops);
	if (WARN_ON(!d_test)) {
		debugfs_remove(d_pita);
		return;
	}

	if (WARN_ON(perf_pmu_register(&pita_pmu, "pita", -1))) {
		debugfs_remove(d_test);
		debugfs_remove(d_pita);
		return;
	}

	attr.type = pita_pmu.type;
	initialized = true;
}
