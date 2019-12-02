#!/bin/bash
# SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

case $1 in
	-h|--help)
		echo -e "$0 [-j <n>]"
		echo -e "\tTest the different ways of building bpftool."
		echo -e ""
		echo -e "\tOptions:"
		echo -e "\t\t-j <n>:\tPass -j flag to 'make'."
		exit 0
		;;
esac

J=$*

# Assume script is located under tools/testing/selftests/bpf/. We want to start
# build attempts from the top of kernel repository.
SCRIPT_REL_PATH=$(realpath --relative-to=$PWD $0)
SCRIPT_REL_DIR=$(dirname $SCRIPT_REL_PATH)
KDIR_ROOT_DIR=$(realpath $PWD/$SCRIPT_REL_DIR/../../../../)
cd $KDIR_ROOT_DIR
if [ ! -e tools/bpf/bpftool/Makefile ]; then
	echo -e "skip:    bpftool files not found!\n"
	exit 0
fi

ERROR=0
TMPDIR=

# If one build fails, continue but return non-0 on exit.
return_value() {
	if [ -d "$TMPDIR" ] ; then
		rm -rf -- $TMPDIR
	fi
	exit $ERROR
}
trap return_value EXIT

check() {
	local dir=$(realpath $1)

	echo -n "binary:  "
	# Returns non-null if file is found (and "false" is run)
	find $dir -type f -executable -name bpftool -print -exec false {} + && \
		ERROR=1 && printf "FAILURE: Did not find bpftool\n"
}

make_and_clean() {
	echo -e "\$PWD:    $PWD"
	echo -e "command: make -s $* >/dev/null"
	make $J -s $* >/dev/null
	if [ $? -ne 0 ] ; then
		ERROR=1
	fi
	if [ $# -ge 1 ] ; then
		check ${@: -1}
	else
		check .
	fi
	(
		if [ $# -ge 1 ] ; then
			cd ${@: -1}
		fi
		make -s clean
	)
	echo
}

make_with_tmpdir() {
	local ARGS

	TMPDIR=$(mktemp -d)
	if [ $# -ge 2 ] ; then
		ARGS=${@:1:(($# - 1))}
	fi
	echo -e "\$PWD:    $PWD"
	echo -e "command: make -s $ARGS ${@: -1}=$TMPDIR/ >/dev/null"
	make $J -s $ARGS ${@: -1}=$TMPDIR/ >/dev/null
	if [ $? -ne 0 ] ; then
		ERROR=1
	fi
	check $TMPDIR
	rm -rf -- $TMPDIR
	echo
}

# Assumes current directory is tools/bpf/bpftool
make_with_dynamic_libbpf() {
	TMPDIR=$(mktemp -d)
	echo -e "\$PWD:    $PWD"

	# It might be needed to clean build tree first because features
	# framework does not detect the change properly
	echo -e "command: make -s -C ../../build/feature clean >/dev/null"
	make $J -s -C ../../build/feature clean >/dev/null
	if [ $? -ne 0 ] ; then
		ERROR=1
	fi
	echo -e "command: make -s -C ../../lib/bpf clean >/dev/null"
	make $J -s -C ../../lib/bpf clean >/dev/null
	if [ $? -ne 0 ] ; then
		ERROR=1
	fi

	# Now install libbpf into TMPDIR
	echo -e "command: make -s -C ../../lib/bpf prefix=$TMPDIR install_lib install_headers >/dev/null"
	make $J -s -C ../../lib/bpf prefix=$TMPDIR install_lib install_headers >/dev/null
	if [ $? -ne 0 ] ; then
		ERROR=1
	fi

	# And final bpftool build (with clean first) with libbpf dynamic link
	echo -e "command: make -s clean >/dev/null"
	if [ $? -ne 0 ] ; then
		ERROR=1
	fi
	echo -e "command: make -s LIBBPF_DYNAMIC=1 LIBBPF_DIR=$TMPDIR >/dev/null"
	make $J -s LIBBPF_DYNAMIC=1 LIBBPF_DIR=$TMPDIR >/dev/null
	if [ $? -ne 0 ] ; then
		ERROR=1
	fi

	check .
	ldd bpftool | grep -q libbpf.so
	if [ $? -ne 0 ] ; then
		printf "FAILURE: Did not find libbpf linked\n"
	else
		echo "binary:  linked with libbpf"
	fi
	make -s -C ../../lib/bpf clean
	make -s clean
	rm -rf -- $TMPDIR
	echo
}

echo "Trying to build bpftool"
echo -e "... through kbuild\n"

if [ -f ".config" ] ; then
	make_and_clean tools/bpf

	## $OUTPUT is overwritten in kbuild Makefile, and thus cannot be passed
	## down from toplevel Makefile to bpftool's Makefile.

	# make_with_tmpdir tools/bpf OUTPUT
	echo -e "skip:    make tools/bpf OUTPUT=<dir> (not supported)\n"

	make_with_tmpdir tools/bpf O
else
	echo -e "skip:    make tools/bpf (no .config found)\n"
	echo -e "skip:    make tools/bpf OUTPUT=<dir> (not supported)\n"
	echo -e "skip:    make tools/bpf O=<dir> (no .config found)\n"
fi

echo -e "... from kernel source tree\n"

make_and_clean -C tools/bpf/bpftool

make_with_tmpdir -C tools/bpf/bpftool OUTPUT

make_with_tmpdir -C tools/bpf/bpftool O

echo -e "... from tools/\n"
cd tools/

make_and_clean bpf

## In tools/bpf/Makefile, function "descend" is called and passes $(O) and
## $(OUTPUT). We would like $(OUTPUT) to have "bpf/bpftool/" appended before
## calling bpftool's Makefile, but this is not the case as the "descend"
## function focuses on $(O)/$(subdir). However, in the present case, updating
## $(O) to have $(OUTPUT) recomputed from it in bpftool's Makefile does not
## work, because $(O) is not defined from command line and $(OUTPUT) is not
## updated in tools/scripts/Makefile.include.
##
## Workarounds would require to a) edit "descend" or use an alternative way to
## call bpftool's Makefile, b) modify the conditions to update $(OUTPUT) and
## other variables in tools/scripts/Makefile.include (at the risk of breaking
## the build of other tools), or c) append manually the "bpf/bpftool" suffix to
## $(OUTPUT) in bpf's Makefile, which may break if targets for other directories
## use "descend" in the future.

# make_with_tmpdir bpf OUTPUT
echo -e "skip:    make bpf OUTPUT=<dir> (not supported)\n"

make_with_tmpdir bpf O

echo -e "... from bpftool's dir\n"
cd bpf/bpftool

make_and_clean

make_with_tmpdir OUTPUT

make_with_tmpdir O

echo -e "... with dynamic libbpf\n"

make_with_dynamic_libbpf
