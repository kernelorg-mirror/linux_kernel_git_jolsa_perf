include ../../scripts/Makefile.include
include ../../scripts/utilities.mak

all:

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

ifeq ($(V),1)
  Q =
else
  Q = @
endif

MAKEFLAGS= --no-print-directory -r
BPF_DIR = $(srctree)/tools/lib/bpf/

ifneq ($(OUTPUT),)
  BPF_PATH = $(OUTPUT)
else
  BPF_PATH = $(BPF_DIR)
endif

LIBBPF = $(BPF_PATH)libbpf.a

BPFTOOL_VERSION=$(shell make --no-print-directory -sC ../../.. kernelversion)

$(LIBBPF): FORCE
	$(Q)$(MAKE) -C $(BPF_DIR) OUTPUT=$(OUTPUT) $(OUTPUT)libbpf.a FEATURES_DUMP=$(FEATURE_DUMP_EXPORT)

$(LIBBPF)-clean:
	$(call QUIET_CLEAN, libbpf)
	$(Q)$(MAKE) -C $(BPF_DIR) OUTPUT=$(OUTPUT) clean >/dev/null

ifdef FIXDEP
  force_fixdep := 0
else
  force_fixdep := 1
endif

export srctree OUTPUT CC CFLAGS LIBS INSTALL RM V

include $(srctree)/tools/build/Makefile.include

ifeq ($(force_fixdep),1)
goals := $(filter-out all sub-make, $(MAKECMDGOALS))

$(goals) all: sub-make

sub-make: fixdep
	$(Q)$(MAKE) FIXDEP=1 -f Makefile $(goals)

else # force_fixdep

prefix ?= /usr/local
bash_compdir ?= /usr/share/bash-completion/completions

CC = gcc

CFLAGS += -O2
CFLAGS += -W -Wall -Wextra -Wno-unused-parameter -Wshadow -Wno-missing-field-initializers
CFLAGS += -DPACKAGE='"bpftool"' -D__EXPORTED_HEADERS__ -I$(srctree)/tools/include/uapi -I$(srctree)/tools/include -I$(srctree)/tools/lib/bpf -I$(srctree)/kernel/bpf/
CFLAGS += -DBPFTOOL_VERSION='"$(BPFTOOL_VERSION)"'
LIBS = -lelf -lbfd -lopcodes $(LIBBPF)

INSTALL ?= install
RM ?= rm -f

FEATURE_USER = .bpftool
FEATURE_TESTS = libbfd disassembler-four-args
FEATURE_DISPLAY = libbfd disassembler-four-args

check_feat := 1
NON_CHECK_FEAT_TARGETS := clean uninstall doc doc-clean doc-install doc-uninstall
ifdef MAKECMDGOALS
ifeq ($(filter-out $(NON_CHECK_FEAT_TARGETS),$(MAKECMDGOALS)),)
  check_feat := 0
endif
endif

ifeq ($(check_feat),1)
ifeq ($(FEATURES_DUMP),)
include $(srctree)/tools/build/Makefile.feature
else
include $(FEATURES_DUMP)
endif
endif

ifeq ($(feature-disassembler-four-args), 1)
CFLAGS += -DDISASM_FOUR_ARGS_SIGNATURE
endif

# get relative building directory (to $(OUTPUT))
# and '.' if it's $(OUTPUT) itself
__build-dir = $(subst $(OUTPUT),,$(dir $@))
build-dir   = $(if $(__build-dir),$(__build-dir),.)

$(OUTPUT)%.o: %.c FORCE
	$(Q)$(MAKE) -f $(srctree)/tools/build/Makefile.build dir=$(build-dir) $@

$(OUTPUT)%.i: %.c FORCE
	$(Q)$(MAKE) -f $(srctree)/tools/build/Makefile.build dir=$(build-dir) $@

$(OUTPUT)%.s: %.c FORCE
	$(Q)$(MAKE) -f $(srctree)/tools/build/Makefile.build dir=$(build-dir) $@

$(OUTPUT)%-bison.o: %.c FORCE
	$(Q)$(MAKE) -f $(srctree)/tools/build/Makefile.build dir=$(build-dir) $@

$(OUTPUT)%-flex.o: %.c FORCE
	$(Q)$(MAKE) -f $(srctree)/tools/build/Makefile.build dir=$(build-dir) $@

$(OUTPUT)%.o: %.S FORCE
	$(Q)$(MAKE) -f $(srctree)/tools/build/Makefile.build dir=$(build-dir) $@

$(OUTPUT)%.i: %.S FORCE
	$(Q)$(MAKE) -f $(srctree)/tools/build/Makefile.build dir=$(build-dir) $@

all: $(OUTPUT)bpftool

$(OUTPUT)bpftool-in.o: FORCE
	$(Q)$(MAKE) $(build)=bpftool

$(OUTPUT)bpftool: $(OUTPUT)bpftool-in.o $(LIBBPF)
	$(QUIET_LINK)$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean: $(LIBBPF)-clean fixdep-clean
	$(call QUIET_CLEAN, bpftool)
	$(Q)$(RM) $(OUTPUT)bpftool $(OUTPUT)*.o $(OUTPUT)*.d
	$(call QUIET_CLEAN, core-gen)
	$(Q)$(RM) $(OUTPUT)FEATURE-DUMP.bpftool

install: $(OUTPUT)bpftool
	$(call QUIET_INSTALL, bpftool)
	$(Q)$(INSTALL) -m 0755 -d $(DESTDIR)$(prefix)/sbin
	$(Q)$(INSTALL) $(OUTPUT)bpftool $(DESTDIR)$(prefix)/sbin/bpftool
	$(Q)$(INSTALL) -m 0755 -d $(DESTDIR)$(bash_compdir)
	$(Q)$(INSTALL) -m 0644 bash-completion/bpftool $(DESTDIR)$(bash_compdir)

uninstall:
	$(call QUIET_UNINST, bpftool)
	$(Q)$(RM) $(DESTDIR)$(prefix)/sbin/bpftool
	$(Q)$(RM) $(DESTDIR)$(bash_compdir)/bpftool

doc:
	$(call descend,Documentation)

doc-clean:
	$(call descend,Documentation,clean)

doc-install:
	$(call descend,Documentation,install)

doc-uninstall:
	$(call descend,Documentation,uninstall)

FORCE:

.PHONY: all FORCE clean install uninstall
.PHONY: doc doc-clean doc-install doc-uninstall

endif # force_fixdep
