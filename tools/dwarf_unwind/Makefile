ifeq ($(V),1)
  Q =
else
  Q = @
endif

ifeq ($(srctree),)
  srctree := $(patsubst %/,%,$(dir $(shell pwd)))
  srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

include $(srctree)/tools/scripts/Makefile.arch
include $(srctree)/tools/scripts/Makefile.include

CFLAGS += -I$(srctree)/tools/include
CFLAGS += -I$(srctree)/tools/lib
CFLAGS += -I$(srctree)/include/uapi
CFLAGS += -I$(srctree)/arch/$(SRCARCH)/include/uapi
CFLAGS += -g

RM         = rm -f
LN         = ln -f
MKDIR      = mkdir
FIND       = find
CC         = gcc
MAKEFLAGS  =--no-print-directory

ifndef DEBUG
  DEBUG := 0
endif

ifeq ($(DEBUG),0)
  CFLAGS += -O6
endif

SUBCMD_DIR = $(srctree)/tools/lib/subcmd/

ifneq ($(OUTPUT),)
  SUBCMD_PATH=$(OUTPUT)
else
  SUBCMD_PATH=$(SUBCMD_DIR)
endif

CONVERT    = $(OUTPUT)convert
CONVERT_IN = $(OUTPUT)convert-in.o

LIBSUBCMD  = $(SUBCMD_PATH)libsubcmd.a

all:

export srctree CC LD AR CFLAGS V
include $(srctree)/tools/build/Makefile.include

all: $(CONVERT)

$(LIBSUBCMD): fixdep FORCE
	$(Q)$(MAKE) -C $(SUBCMD_DIR) O=$(OUTPUT) $(OUTPUT)libsubcmd.a

$(CONVERT_IN): FORCE
	$(Q)$(MAKE) $(build)=convert

$(CONVERT): $(CONVERT_IN) $(LIBSUBCMD)
	$(QUIET_LINK)$(CC) -o $@ $^ -lelf

clean:
	$(Q)find $(if $(OUTPUT),$(OUTPUT),.) -name '*.o' -delete -o -name '\.*.cmd' -delete -o -name '\.*.d' -delete
	$(call QUIET_CLEAN, convert)
	$(Q)$(RM) $(CONVERT)

TAG_FOLDERS = . ../lib ../include
TAG_FILES   = ../../include/uapi/linux/dwarf_unwind.h

tags:
	$(QUIET_GEN)$(RM) tags; \
	$(FIND) $(TAG_FOLDERS) -name '*.[hcS]' -print | xargs ctags -a $(TAG_FILES)

cscope:
	$(QUIET_GEN)$(RM) cscope*; \
	$(FIND) $(TAG_FOLDERS) -name '*.[hcS]' -print | xargs cscope -b $(TAG_FILES)


FORCE:

.PHONY: FORCE tags cscope clean
