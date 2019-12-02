# SPDX-License-Identifier: GPL-2.0-only

# To link dynamically with libbpf run:
#   make LIBBPF_DYNAMIC=1
# build will try to detect installed devel package of libbpf.
#
# For linking dynamically with libbpf installed at specific PATH run:
#   make LIBBPF_DYNAMIC=1 LIBBPF_DIR=<PATH>

include ../../scripts/Makefile.include
include ../../scripts/utilities.mak
include ../../scripts/Makefile.arch

ifeq ($(LP64), 1)
  libdir_relative = lib64
else
  libdir_relative = lib
endif

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

LIBBPF_SRC_DIR = $(srctree)/tools/lib/bpf/

ifneq ($(OUTPUT),)
  LIBBPF_OUTPUT = $(OUTPUT)/libbpf/
  LIBBPF_PATH = $(LIBBPF_OUTPUT)
else
  LIBBPF_PATH = $(LIBBPF_SRC_DIR)
endif

LIBBPF = $(LIBBPF_PATH)libbpf.a

BPFTOOL_VERSION := $(shell make -rR --no-print-directory -sC ../../.. kernelversion)

$(LIBBPF): FORCE
	$(if $(LIBBPF_OUTPUT),@mkdir -p $(LIBBPF_OUTPUT))
	$(Q)$(MAKE) -C $(LIBBPF_SRC_DIR) OUTPUT=$(LIBBPF_OUTPUT) $(LIBBPF_OUTPUT)libbpf.a

$(LIBBPF)-clean:
	$(call QUIET_CLEAN, libbpf)
	$(Q)$(MAKE) -C $(LIBBPF_SRC_DIR) OUTPUT=$(LIBBPF_OUTPUT) clean >/dev/null

prefix ?= /usr/local
bash_compdir ?= /usr/share/bash-completion/completions

CFLAGS += -O2
CFLAGS += -W -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
CFLAGS += $(filter-out -Wswitch-enum,$(EXTRA_WARNINGS))
CFLAGS += -DPACKAGE='"bpftool"' -D__EXPORTED_HEADERS__ \
	-I$(srctree)/kernel/bpf/ \
	-I$(srctree)/tools/include \
	-I$(srctree)/tools/include/uapi \
	-I$(srctree)/tools/lib/bpf \
	-I$(srctree)/tools/perf
CFLAGS += -DBPFTOOL_VERSION='"$(BPFTOOL_VERSION)"'
ifneq ($(EXTRA_CFLAGS),)
CFLAGS += $(EXTRA_CFLAGS)
endif
ifneq ($(EXTRA_LDFLAGS),)
LDFLAGS += $(EXTRA_LDFLAGS)
endif

LIBS = $(LIBBPF) -lelf -lz

INSTALL ?= install
RM ?= rm -f

FEATURE_USER = .bpftool
FEATURE_TESTS = libbfd disassembler-four-args reallocarray zlib
FEATURE_DISPLAY = libbfd disassembler-four-args zlib
ifdef LIBBPF_DYNAMIC
  FEATURE_TESTS   += libbpf
  FEATURE_DISPLAY += libbpf

  ifdef LIBBPF_DIR
    LIBBPF_CFLAGS  := -I$(LIBBPF_DIR)/include
    LIBBPF_LDFLAGS := -L$(LIBBPF_DIR)/$(libdir_relative)
    FEATURE_CHECK_CFLAGS-libbpf  := $(LIBBPF_CFLAGS)
    FEATURE_CHECK_LDFLAGS-libbpf := $(LIBBPF_LDFLAGS)
  endif
endif

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

ifeq ($(feature-reallocarray), 0)
CFLAGS += -DCOMPAT_NEED_REALLOCARRAY
endif

ifdef LIBBPF_DYNAMIC
  ifeq ($(feature-libbpf), 1)
    # bpftool uses non-exported functions from libbpf, so just add the dynamic
    # version of libbpf and let the linker figure it out
    LIBS    := -lbpf $(LIBS)
    CFLAGS  += $(LIBBPF_CFLAGS)
    LDFLAGS += $(LIBBPF_LDFLAGS)
  else
    dummy := $(error Error: No libbpf devel library found, please install libbpf-devel or libbpf-dev.)
  endif
endif

include $(wildcard $(OUTPUT)*.d)

all: $(OUTPUT)bpftool

BFD_SRCS = jit_disasm.c

SRCS = $(filter-out $(BFD_SRCS),$(wildcard *.c))

ifeq ($(feature-libbfd),1)
  LIBS += -lbfd -ldl -lopcodes
else ifeq ($(feature-libbfd-liberty),1)
  LIBS += -lbfd -ldl -lopcodes -liberty
else ifeq ($(feature-libbfd-liberty-z),1)
  LIBS += -lbfd -ldl -lopcodes -liberty -lz
endif

ifneq ($(filter -lbfd,$(LIBS)),)
CFLAGS += -DHAVE_LIBBFD_SUPPORT
SRCS += $(BFD_SRCS)
endif

OBJS = $(patsubst %.c,$(OUTPUT)%.o,$(SRCS)) $(OUTPUT)disasm.o

$(OUTPUT)disasm.o: $(srctree)/kernel/bpf/disasm.c
	$(QUIET_CC)$(COMPILE.c) -MMD -o $@ $<

$(OUTPUT)feature.o: | zdep

$(OUTPUT)bpftool: $(OBJS) $(LIBBPF)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(OUTPUT)%.o: %.c
	$(QUIET_CC)$(COMPILE.c) -MMD -o $@ $<

clean: $(LIBBPF)-clean
	$(call QUIET_CLEAN, bpftool)
	$(Q)$(RM) -- $(OUTPUT)bpftool $(OUTPUT)*.o $(OUTPUT)*.d
	$(Q)$(RM) -r -- $(OUTPUT)libbpf/
	$(call QUIET_CLEAN, core-gen)
	$(Q)$(RM) -- $(OUTPUT)FEATURE-DUMP.bpftool
	$(Q)$(RM) -r -- $(OUTPUT)feature/

install: $(OUTPUT)bpftool
	$(call QUIET_INSTALL, bpftool)
	$(Q)$(INSTALL) -m 0755 -d $(DESTDIR)$(prefix)/sbin
	$(Q)$(INSTALL) $(OUTPUT)bpftool $(DESTDIR)$(prefix)/sbin/bpftool
	$(Q)$(INSTALL) -m 0755 -d $(DESTDIR)$(bash_compdir)
	$(Q)$(INSTALL) -m 0644 bash-completion/bpftool $(DESTDIR)$(bash_compdir)

uninstall:
	$(call QUIET_UNINST, bpftool)
	$(Q)$(RM) -- $(DESTDIR)$(prefix)/sbin/bpftool
	$(Q)$(RM) -- $(DESTDIR)$(bash_compdir)/bpftool

doc:
	$(call descend,Documentation)

doc-clean:
	$(call descend,Documentation,clean)

doc-install:
	$(call descend,Documentation,install)

doc-uninstall:
	$(call descend,Documentation,uninstall)

FORCE:

zdep:
	@if [ "$(feature-zlib)" != "1" ]; then echo "No zlib found"; exit 1 ; fi

.PHONY: all FORCE clean install uninstall zdep
.PHONY: doc doc-clean doc-install doc-uninstall
.DEFAULT_GOAL := all
