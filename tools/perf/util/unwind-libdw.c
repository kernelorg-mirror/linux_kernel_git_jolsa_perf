#include <linux/compiler.h>
#include <elfutils/libdwfl.h>
#include <inttypes.h>
#include <errno.h>
#include "unwind.h"
#include "machine.h"
#include "thread.h"
#include "types.h"
#include "event.h"
#include "perf_regs.h"

struct unwind_info {
	Dwfl			*dwfl;
	struct perf_sample      *sample;
	struct machine          *machine;
	struct thread           *thread;
	u64                     sample_uregs;
	unwind_entry_cb_t	cb;
	void			*arg;
	int			max_stack;
};

static char *debuginfo_path;

static const Dwfl_Callbacks offline_callbacks = {
	.find_debuginfo		= dwfl_standard_find_debuginfo,
	.debuginfo_path		= &debuginfo_path,
	.section_address	= dwfl_offline_section_address,
};

static int entry(u64 ip, struct unwind_info *ui)

{
	struct unwind_entry e;
	struct addr_location al;

	thread__find_addr_location(ui->thread, ui->machine,
				   PERF_RECORD_MISC_USER,
				   MAP__FUNCTION, ip, &al);

	if (al.map && al.map->dso) {
		char *name = al.map->dso->long_name;

		dwfl_report_elf(ui->dwfl, name, name, -1,
				al.map->start, false);
	}

	e.ip = ip;
	e.map = al.map;
	e.sym = al.sym;

	pr_debug("unwind: %s:ip = 0x%" PRIx64 " (0x%" PRIx64 ")\n",
		 al.sym ? al.sym->name : "''",
		 ip,
		 al.map ? al.map->map_ip(al.map, ip) : (u64) 0);

	return ui->cb(&e, ui->arg);
}

static pid_t next_thread(Dwfl *dwfl, void *arg __maybe_unused,
			 void **thread_argp)
{
	if (*thread_argp != NULL)
		return 0;

	*thread_argp = thread_argp;
	return dwfl_pid(dwfl);
}

static int access_dso_mem(struct unwind_info *ui, Dwarf_Addr addr,
			  Dwarf_Word *data)
{
	struct addr_location al;
	ssize_t size;

	thread__find_addr_map(ui->thread, ui->machine, PERF_RECORD_MISC_USER,
			      MAP__FUNCTION, addr, &al);
	if (!al.map) {
		pr_debug("unwind: no map for %lx\n", (unsigned long)addr);
		return -1;
	}

	if (!al.map->dso)
		return -1;

	size = dso__data_read_addr(al.map->dso, al.map, ui->machine,
				   addr, (u8 *) data, sizeof(*data));

	return !(size == sizeof(*data));
}

static bool memory_read(Dwfl *dwfl __maybe_unused, Dwarf_Addr addr, Dwarf_Word *result,
			void *arg)
{
	struct unwind_info *ui = arg;
	struct stack_dump *stack = &ui->sample->user_stack;
	u64 start, end;
	int offset;
	int ret;

	ret = perf_reg_value(&start, &ui->sample->user_regs, PERF_REG_SP,
			     ui->sample_uregs);
	if (ret)
		return false;

	end = start + stack->size;

	/* Check overflow. */
	if (addr + sizeof(Dwarf_Word) < addr)
		return false;

	if (addr < start || addr + sizeof(Dwarf_Word) >= end) {
		ret = access_dso_mem(ui, addr, result);
		if (ret) {
			pr_debug("unwind: access_mem %p not inside range %p-%p\n",
				(void *)addr, (void *)start, (void *)end);
			*result = 0;
			return false;
		}
		return 0;
	}

	offset  = addr - start;
	*result = *(Dwarf_Word *)&stack->data[offset];
	pr_debug("unwind: access_mem addr %p, val %lx, offset %d\n",
		 (void *)addr, (unsigned long)*result, offset);
	return true;
}


static bool
unwind_libdw__arch_set_initial_registers(Dwfl_Thread *thread,
					 void *arg)
{
	struct unwind_info *ui = arg;
	struct perf_sample *data = ui->sample;
	Dwarf_Word dwarf_regs[17];

#define REG(r) ({			\
	Dwarf_Word val = 0;		\
	perf_reg_value(&val, &data->user_regs, PERF_REG_X86_##r, ui->sample_uregs); \
	val;				\
})

	dwarf_regs[0] = REG(AX);
	dwarf_regs[1] = REG(DX);
	dwarf_regs[2] = REG(CX);
	dwarf_regs[3] = REG(BX);
	dwarf_regs[4] = REG(SI);
	dwarf_regs[5] = REG(DI);
	dwarf_regs[6] = REG(BP);
	dwarf_regs[7] = REG(SP);
	dwarf_regs[8] = REG(R8);
	dwarf_regs[9] = REG(R9);
	dwarf_regs[10] = REG(R10);
	dwarf_regs[11] = REG(R11);
	dwarf_regs[12] = REG(R12);
	dwarf_regs[13] = REG(R13);
	dwarf_regs[14] = REG(R14);
	dwarf_regs[15] = REG(R15);
	dwarf_regs[16] = REG(IP);

	return dwfl_thread_state_registers(thread, 0, 17, dwarf_regs);
}

static const Dwfl_Thread_Callbacks callbacks = {
	.next_thread		= next_thread,
	.memory_read		= memory_read,
	.set_initial_registers	= unwind_libdw__arch_set_initial_registers,
};

static int
frame_callback (Dwfl_Frame *state, void *arg)
{
	struct unwind_info *ui = arg;
	Dwarf_Addr pc, pc_adjusted;
	bool isactivation;

	if (!dwfl_frame_pc(state, &pc, &isactivation)) {
		pr_err("%s", dwfl_errmsg (-1));
		return -1;
	}

	pc_adjusted = pc - (isactivation ? 0 : 1);

	return entry(pc_adjusted, ui) ?
	       DWARF_CB_ABORT : DWARF_CB_OK;
}

static int thread_callback(Dwfl_Thread *thread, void *arg)
{
	return dwfl_thread_getframes (thread, frame_callback, arg) ?
	       DWARF_CB_ABORT : DWARF_CB_OK;
}

int unwind__get_entries(unwind_entry_cb_t cb, void *arg,
                        struct machine *machine, struct thread *thread,
                        u64 sample_uregs, struct perf_sample *data,
                        int max_stack)
{
	Dwfl *dwfl = dwfl_begin(&offline_callbacks);
	struct unwind_info ui = {
		.dwfl		= dwfl,
		.sample		= data,
		.sample_uregs	= sample_uregs,
		.thread		= thread,
		.machine	= machine,
		.cb		= cb,
		.arg		= arg,
		.max_stack	= max_stack,
	};
	Dwarf_Word ip;
	int err;

	if (!dwfl) {
		pr_warning("unwind: failed to initialize libdw.\n");
		return -1;
	}

	if (!data->user_regs.regs)
		return -EINVAL;

	err = perf_reg_value(&ip, &data->user_regs, PERF_REG_IP, sample_uregs);
	if (err)
		return err;

	err = entry(ip, &ui);
	if (err)
		return -ENOMEM;

	if (!dwfl_attach_state(dwfl, EM_NONE, thread->tid, &callbacks, NULL))
		return -EINVAL;

	err = dwfl_getthreads(dwfl, thread_callback, &ui);

	dwfl_end (dwfl);
	return err;
}
