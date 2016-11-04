ifeq ($(V),1)
  Q =
else
  Q = @
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
	rm -f convert convert-in.o convert.o

FORCE:

.PHONY: FORCE
