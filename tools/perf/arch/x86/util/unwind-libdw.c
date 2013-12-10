#include <elfutils/libdwfl.h>
#include "../../util/unwind-libdw.h"
#include "../../util/perf_regs.h"

#ifdef HAVE_ARCH_X86_64_SUPPORT
#define NREGS 17
#else
#define NREGS 9
#endif

bool libdw__arch_set_initial_registers(Dwfl_Thread *thread, void *arg)
{
	struct unwind_info *ui = arg;
	struct perf_sample *data = ui->sample;
	Dwarf_Word dwarf_regs[NREGS];

#define REG(r) ({			\
	Dwarf_Word val = 0;		\
	perf_reg_value(&val, &data->user_regs, PERF_REG_X86_##r, ui->sample_uregs); \
	val;				\
})

#ifdef HAVE_ARCH_X86_64_SUPPORT
	dwarf_regs[0]  = REG(AX);
	dwarf_regs[1]  = REG(DX);
	dwarf_regs[2]  = REG(CX);
	dwarf_regs[3]  = REG(BX);
	dwarf_regs[4]  = REG(SI);
	dwarf_regs[5]  = REG(DI);
	dwarf_regs[6]  = REG(BP);
	dwarf_regs[7]  = REG(SP);
	dwarf_regs[8]  = REG(R8);
	dwarf_regs[9]  = REG(R9);
	dwarf_regs[10] = REG(R10);
	dwarf_regs[11] = REG(R11);
	dwarf_regs[12] = REG(R12);
	dwarf_regs[13] = REG(R13);
	dwarf_regs[14] = REG(R14);
	dwarf_regs[15] = REG(R15);
	dwarf_regs[16] = REG(IP);
#else
	dwarf_regs[0] = REG(AX);
	dwarf_regs[1] = REG(CX);
	dwarf_regs[2] = REG(DX);
	dwarf_regs[3] = REG(BX);
	dwarf_regs[4] = REG(SP);
	dwarf_regs[5] = REG(BP);
	dwarf_regs[6] = REG(SI);
	dwarf_regs[7] = REG(DI);
	dwarf_regs[8] = REG(IP);
#endif

	return dwfl_thread_state_registers(thread, 0, NREGS, dwarf_regs);
}
