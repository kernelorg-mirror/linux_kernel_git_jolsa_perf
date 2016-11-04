ifeq ($(V),1)
  Q =
else
  Q = @
endif

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(shell pwd)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
#$(info Determined 'srctree' to be $(srctree))
endif

include $(srctree)/tools/scripts/Makefile.arch

CFLAGS += -I$(srctree)/tools/include
CFLAGS += -I$(srctree)/include/uapi
CFLAGS += -I$(srctree)/arch/$(SRCARCH)/include/uapi
CFLAGS += -g

ifndef DEBUG
  DEBUG := 0
endif

ifeq ($(DEBUG),0)
  CFLAGS += -O6
endif

RM        = rm -f
LN        = ln -f
MKDIR     = mkdir
FIND      = find
CC        = gcc
MAKEFLAGS =--no-print-directory

all:

include $(srctree)/tools/scripts/Makefile.include

export srctree CC LD AR CFLAGS V
include $(srctree)/tools/build/Makefile.include

convert-in.o: FORCE
	$(Q)$(MAKE) $(build)=convert

convert: convert-in.o
	$(QUIET_LINK)$(CC) -o $@ $< -lelf

all: convert

clean:
	$(Q)find $(if $(OUTPUT),$(OUTPUT),.) -name '*.o' -delete -o -name '\.*.cmd' -delete -o -name '\.*.d' -delete

TAG_FOLDERS= . ../lib ../include
TAG_FILES= ../../include/uapi/linux/perf_event.h

tags:
	$(QUIET_GEN)$(RM) tags; \
	$(FIND) $(TAG_FOLDERS) -name '*.[hcS]' -print | xargs ctags -a $(TAG_FILES)

cscope:
	$(QUIET_GEN)$(RM) cscope*; \
	$(FIND) $(TAG_FOLDERS) -name '*.[hcS]' -print | xargs cscope -b $(TAG_FILES)


FORCE:

.PHONY: FORCE tags cscope
