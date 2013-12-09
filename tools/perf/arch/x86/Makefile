ifndef NO_DWARF
PERF_HAVE_DWARF_REGS := 1
LIB_OBJS += $(OUTPUT)arch/$(ARCH)/util/dwarf-regs.o
endif
ifndef NO_LIBUNWIND
LIB_OBJS += $(OUTPUT)arch/$(ARCH)/util/unwind-libunwind.o
endif
LIB_OBJS += $(OUTPUT)arch/$(ARCH)/util/header.o
LIB_OBJS += $(OUTPUT)arch/$(ARCH)/util/tsc.o
ifndef NO_LIBUNWIND
ifeq ($(RAW_ARCH),x86_64)
LIB_OBJS += $(OUTPUT)arch/$(ARCH)/tests/regs_load_64.o
else
LIB_OBJS += $(OUTPUT)arch/$(ARCH)/tests/regs_load_32.o
endif
LIB_OBJS += $(OUTPUT)arch/$(ARCH)/tests/dwarf-unwind.o
endif # NO_LIBUNWIND
LIB_H += arch/$(ARCH)/util/tsc.h
