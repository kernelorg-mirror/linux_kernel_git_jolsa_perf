#!/bin/sh

perf=$1
cache=$2
verbose=$3

function pr_debug
{
	if [ "$verbose" -gt "0" ]; then
		echo "$@"
	fi
}

function run_perf
{
	$perf --no-pager --buildid-dir $cache $@
}

# Remove prepared '[kernel.kallsyms]' via 1 byte clean limit
run_perf buildid-cache clean -a -r 1B 2>/dev/null
dir="$cache/[kernel.kallsyms]"
if [ "$(ls -A $cache)" ]; then
	pr_debug "Failed to remove [kernel.kallsyms] cache files"
	exit 1
fi

# add perf binary
run_perf buildid-cache -a $perf 2>/dev/null
dir_perf=$cache/`realpath $perf`
if [ ! -d $dir_perf ]; then
	pr_debug "Failed to add perf binary into cache"
	exit 1
fi

# remove perf binary
run_perf buildid-cache -r $perf 2>/dev/null
if [ "$(ls -A $cache)" ]; then
	pr_debug "Failed to remove perf binary from cache"
	exit 1
fi

# add perf binary
run_perf buildid-cache -a $perf 2>/dev/null
# add sh binary
run_perf buildid-cache -a `realpath /bin/sh` 2>/dev/null
dir_sh=$cache/`realpath /bin/sh`
if [ ! -d $dir_perf -o ! -d $dir_sh ]; then
	pr_debug "Failed to add perf/sh binary into cache"
	exit 1
fi

# clean all
run_perf buildid-cache clean -r 2>/dev/null
if [ "$(ls -A $cache)" ]; then
	pr_debug "Failed to cleanup the cache"
	exit 1
fi

# last command, $cache directory should be empty
rmdir $cache
