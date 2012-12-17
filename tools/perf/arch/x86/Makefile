ifndef NO_DWARF
PERF_HAVE_DWARF_REGS := 1
LIB_OBJS += $(OUTPUT)arch/$(ARCH)/util/dwarf-regs.o
endif
ifndef NO_LIBUNWIND
LIB_OBJS += $(OUTPUT)arch/$(ARCH)/util/unwind.o
endif
LIB_OBJS += $(OUTPUT)arch/$(ARCH)/util/header.o
LIB_OBJS += $(OUTPUT)arch/$(ARCH)/util/pmu.o

$(OUTPUT)arch/$(ARCH)/util/pmu.o: $(OUTPUT)arch/$(ARCH)/util/pmu.c $(OUTPUT)PERF-CFLAGS
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) \
	 '-DPERF_EXEC_PATH="$(perfexecdir_SQ)"' \
	 '-DPREFIX="$(prefix_SQ)"' \
	 $<
