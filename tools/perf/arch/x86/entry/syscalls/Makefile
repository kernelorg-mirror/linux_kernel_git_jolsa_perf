out := $(OUTPUT)/arch/x86/include/generated/asm
src := arch/x86/entry/syscalls

# Create output directory if not already present
_dummy := $(shell [ -d '$(out)' ] || mkdir -p '$(out)')

syscall64 := $(srctree)/$(src)/syscall_64.tbl

systbl := $(srctree)/$(src)/syscalltbl.sh

quiet_cmd_systbl = SYSTBL  $@
      cmd_systbl = $(CONFIG_SHELL) '$(systbl)' 'x86_64' $< > $@

$(out)/syscalls_64.h: $(syscall64) $(systbl)
	$(call if_changed,systbl)

PHONY += all
