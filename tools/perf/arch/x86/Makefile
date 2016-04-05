ifndef NO_DWARF
PERF_HAVE_DWARF_REGS := 1
endif
HAVE_KVM_STAT_SUPPORT := 1
PERF_HAVE_ARCH_REGS_QUERY_REGISTER_OFFSET := 1
PERF_HAVE_JITDUMP := 1

###
# Syscall table generation
#
header := $(OUTPUT)arch/x86/include/generated/asm/syscalls_64.h
sys    := $(srctree)/tools/perf/arch/x86/entry/syscalls
systbl := $(sys)/syscalltbl.sh

$(header): $(sys)/syscall_64.tbl $(systbl)
	$(SHELL) '$(systbl)' $(sys)/syscall_64.tbl 'x86_64' > $@

clean::
	rm -f $(header)

archheaders: $(header)
