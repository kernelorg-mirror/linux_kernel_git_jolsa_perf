#!/bin/bash
# download event files for current cpu for perf

WGETOPT=${WGETOPT:---no-verbose --timeout 5}

set -e

if ! type wget > /dev/null ; then
	echo "please install wget"
	exit 1
fi

if [ "$1" == "" ] ; then
	S=$(awk '
/^vendor/     		{ V=$3 }
/^model/ && $2 == ":" 	{ M=$3 }
/^cpu family/ 		{ F = $4 }
END	      		{ printf("%s-%s-%X", V, F, M) }' /proc/cpuinfo)
else
	S="$1"
fi
V=$(echo $S  | ( IFS=- read v f m ; echo $v) )

CACHEDIR=${XDG_CACHE_HOME:-~/.cache}
[ ! -d $CACHEDIR/pmu-events ] && mkdir -p $CACHEDIR/pmu-events
cd $CACHEDIR/pmu-events

case "$V" in
GenuineIntel)
	echo "Downloading models file"
	URLBASE=${URLBASE:-https://download.01.org/perfmon}
	MAPFILE=${MAPFILE:-mapfile.csv}
	echo "Downloading readme.txt"
	wget -N $WGETOPT $URLBASE/readme.txt
	;;

# Add more CPU vendors here

*)
	echo "Unsupported CPU vendor $V"
	exit 1
	;;
esac

wget -N $WGETOPT $URLBASE/$MAPFILE

echo "Downloading events file"
awk -v urlbase=$URLBASE -v cpu="$S" -F, \
	'$1 == cpu && $4 == "core" { print urlbase $3; exit 0 }' \
	$MAPFILE > url$$
if [ -s url$$ ] ; then
	wget $WGETOPT -q -i url$$ -O $S-core.json
else
	echo "CPU $S not found"
fi
rm -f url$$

