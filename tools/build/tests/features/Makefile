all: test1

test1:
	rm -f FEATURE-DUMP
	make -f Makefile.test1 > out
	# we should get one line with 'glibc' feature status
	features=`grep -cF ... out`; \
	if [ "$$features" != "1" ]; then echo FAILED; exit 1; fi
	# we should NOT get any feature status line on second run
	make -f Makefile.test1 > out
	features=`grep -cF ... out`; \
	if [ "$$features" == "1" ]; then echo FAILED; exit 1; fi
	# we should get both 'glibc' and 'backtrace' status lines now
	make -f Makefile.test1 VF=1 > out
	features=`grep -cF ... out`; \
	if [ "$$features" != "2" ]; then echo FAILED; exit 1; fi
	# and fresh start without FEATURE-DUMP, expecting 'glibc' status line
	rm -f FEATURE-DUMP
	make -f Makefile.test1 > out
	features=`grep -cF ... out`; \
	if [ "$$features" != "1" ]; then echo FAILED; exit 1; fi
	# cleanup
	rm -f FEATURE-DUMP out
