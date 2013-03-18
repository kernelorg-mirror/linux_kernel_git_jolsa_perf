CONFIG ?= .config-detected

CC      = $(CROSS_COMPILE)gcc
AR      = $(CROSS_COMPILE)ar
RM      = rm -f
MKDIR   = mkdir
FIND    = find
INSTALL = install
FLEX    = flex
BISON   = bison
STRIP  ?= strip

ifndef SHELL_PATH
	SHELL_PATH = /bin/sh
endif
ifndef PERL_PATH
	PERL_PATH = /usr/bin/perl
endif

store = @echo "$(1)=$($(1))" >> $(CONFIG)

config-detect:
	@rm -f $(CONFIG)
	$(call store,CC)
	$(call store,AR)
	$(call store,RM)
	$(call store,MKDIR)
	$(call store,FIND)
	$(call store,INSTALL)
	$(call store,FLEX)
	$(call store,BISON)
	$(call store,STRIP)
	$(call store,SHELL_PATH)
	$(call store,PERL_PATH)
