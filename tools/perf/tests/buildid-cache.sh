#!/bin/sh

perf=$1
cache=$2

function run_perf
{
	$perf --no-pager --buildid-dir $cache $@
}

# Remove prepared '[kernel.kallsyms]' via 1 day clean
run_perf buildid-cache clean -a -r 1w 2>/dev/null
dir="$cache/[kernel.kallsyms]"
if [ -d $dir ]; then
	exit 1
fi

# add perf binary
run_perf buildid-cache -a $perf 2>/dev/null
dir=$cache/`realpath $perf`
if [ ! -d $dir ]; then
	exit 1
fi

# remove perf binary
run_perf buildid-cache -r $perf 2>/dev/null
if [ -d $dir ]; then
	exit 1
fi

# clean all
run_perf buildid-cache clean -r 2>/dev/null

# last command, $cache directory should be empty
rmdir $cache
