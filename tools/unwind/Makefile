ifeq ($(V),1)
  Q =
else
  Q = @
endif

CFLAGS += -I$(srctree)/tools/include -g

ifndef DEBUG
  DEBUG := 0
endif

ifeq ($(DEBUG),0)
  CFLAGS += -O6
endif

CC=gcc
MAKEFLAGS=--no-print-directory

all:

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(shell pwd)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
#$(info Determined 'srctree' to be $(srctree))
endif

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

FORCE:

.PHONY: FORCE
