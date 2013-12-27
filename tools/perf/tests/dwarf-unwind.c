#include <linux/compiler.h>
#include <sys/types.h>
#include <unistd.h>
#include "tests.h"
#include "debug.h"
#include "machine.h"
#include "event.h"
#include "unwind.h"
#include "perf_regs.h"
#include "map.h"
#include "thread.h"

static int mmap_handler(struct perf_tool *tool __maybe_unused,
			union perf_event *event,
			struct perf_sample *sample __maybe_unused,
			struct machine *machine)
{
	return machine__process_mmap_event(machine, event, NULL);
}

static int init_live_machine(struct machine *machine)
{
	union perf_event event;
	pid_t pid = getpid();

	return perf_event__synthesize_mmap_events(NULL, &event, pid, pid,
						  mmap_handler, machine, true);
}

static int unwind_entry(struct unwind_entry *entry, void *arg __maybe_unused)
{
		pr_info("0x%lu\n", entry->ip);

	return 0;
}

static int init_sample(struct perf_sample *sample, struct thread *thread)
{
#define STACK_SIZE 8192
	struct regs_dump *regs_dump = &sample->user_regs;
	struct stack_dump *stack_dump = &sample->user_stack;
	u64 *buf_regs  = malloc(sizeof(u64) * PERF_REG_X86_64_MAX);
	u64 *buf_stack = malloc(STACK_SIZE);
	u64 stack_size;
	struct map *map;
	unsigned long sp;

	if (!buf_regs || !buf_stack) {
		pr_debug("Could not allocate sample data\n");
		return -1;
	}

	perf_regs_load(buf_regs);

	regs_dump->abi  = PERF_SAMPLE_REGS_ABI_64;
	regs_dump->mask = PERF_REGS_MASK;
	regs_dump->regs = buf_regs;

	sp = (unsigned long) buf_regs[PERF_REG_X86_SP];

	map = map_groups__find(&thread->mg, MAP__FUNCTION, (u64) sp);
	if (!map) {
		pr_debug("Could not get stack map\n");
		return -1;
	}

	stack_size = map->end - sp;
	stack_size = stack_size > STACK_SIZE ? STACK_SIZE : stack_size;

	memcpy(buf_stack, (void *) sp, stack_size);
	stack_dump->data = (char *) buf_stack;
	stack_dump->size = stack_size;
	return 0;
}

static void cleanup_sample(struct perf_sample *sample)
{
	free(sample->user_stack.data);
	free(sample->user_regs.regs);
}

static int unwind_thread(struct thread *thread, struct machine *machine, unsigned long *addrs_2 __maybe_unused)
{
	struct perf_sample sample;
	int err;

	if (init_sample(&sample, thread))
		return -1;

	err = unwind__get_entries(unwind_entry, NULL, machine, thread,
				  &sample, -1);
	if (err)
		pr_debug("unwind failed\n");

	cleanup_sample(&sample);
	return err;
}

#define FUNCS 10

#define FUNC(__caller, __callee)				\
static int func ## __caller (struct thread *thread,		\
			     struct machine *machine,		\
			     unsigned long *addrs_1,		\
			     unsigned long *addrs_2)		\
{								\
	int err;						\
	err = func ## __callee (thread, machine, addrs_1, addrs_2);	\
 addr:								\
	addrs_1[__caller] = &addr;				\
	return err;						\
}

#define FUNC_MAX(__idx) \
static int func ## __idx (struct thread *thread,		\
			  struct machine *machine,		\
			  unsigned long *addrs_1 __maybe_unused,		\
			  unsigned long *addrs_2)		\
{								\
	return unwind_thread(thread, machine, addrs_2);		\
}

FUNC_MAX(10)
FUNC(9, 10)
FUNC(8, 9)
FUNC(7, 8)
FUNC(6, 7)
FUNC(5, 6)
FUNC(4, 5)
FUNC(3, 4)
FUNC(2, 3)
FUNC(1, 2)
FUNC(0, 1)

static int unwind_check(struct thread *thread, struct machine *machine)
{
	static unsigned long addrs_1[FUNCS];
	static unsigned long addrs_2[FUNCS];
	int err;

	err = func0(thread, machine, addrs_1, addrs_2);
	if (!err)
		err = memcmp(addrs_1, addrs_2, FUNCS * sizeof(unsigned long));

	return err;
}

int test__dwarf_unwind(void)
{
	struct machines machines;
	struct machine *machine;
	struct thread *thread;
	int err = -1;

	machines__init(&machines);

	machine = machines__find(&machines, HOST_KERNEL_ID);
	if (!machine) {
		pr_err("Could not get machine\n");
		return -1;
	}

	if (verbose > 1)
		machine__fprintf(machine, stderr);

	if (init_live_machine(machine)) {
		pr_err("Could not init machine\n");
		goto out;
	}

	thread = machine__find_thread(machine, getpid());
	if (!thread) {
		pr_err("Could not get thread\n");
		goto out;
	}

	err = unwind_thread(thread, machine);

 out:
	machine__delete_threads(machine);
	machine__exit(machine);
	machines__exit(&machines);
	return err;
}
