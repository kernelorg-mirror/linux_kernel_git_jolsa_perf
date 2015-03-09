export srctree := ../../../..
export CC      := gcc
export LD      := ld
export AR      := ar

build := -f $(srctree)/tools/build/Makefile.build dir=. obj
clean := -f $(srctree)/tools/build/Makefile.clean dir=. obj

ex: ex-in.o libex-in.o
	gcc -o $@ $^

ex.%: FORCE
	make -f $(srctree)/tools/build/Makefile.build dir=. $@

ex-in.o: FORCE
	make $(build)=ex

libex-in.o: FORCE
	make $(build)=libex

clean:
	make $(clean)=ex
	make $(clean)=libex
	find -name '\.*.cmd' -delete -o -name '\.*.d' -delete
	rm -f ex ex.i ex.s

.PHONY: FORCE
