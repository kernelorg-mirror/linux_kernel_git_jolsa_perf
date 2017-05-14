#!/bin/sh

RESCTRL=$1

function add_group
{
	NAME=$1
	ID=$2
	CPUS_LIST=$3
	CPUS=$4
	TASKS1=$5
	TASKS2=$6
	RES1=$7
	BASE=$RESCTRL

	if [ "$NAME" != "default" ]; then
		BASE=$BASE/$NAME
		mkdir $BASE
	fi

	echo $ID         >  $BASE/id
	echo $CPUS_LIST  >> $BASE/cpus_list
	echo $CPUS       >> $BASE/cpus
	echo $TASKS1     >  $BASE/tasks
	echo $TASKS2     >> $BASE/tasks
	echo $RES1       >  $BASE/schemata
}

mkdir -p $RESCTRL/info/L3
echo 7ff > $RESCTRL/info/L3/cbm_mask
echo 1   > $RESCTRL/info/L3/min_cbm_bits
echo 16  > $RESCTRL/info/L3/num_closids

add_group "default" 0 "0-23"  "ffffff" "0"   "1"   "L3:0=7ff;1=7ff;2=7ff;3=7ff"
add_group "krava1"  1 "0-12"  "001fff" "10"  "11"  "L3:0=f;1=f;2=f;3=f"
add_group "krava2"  2 "13-23" "ffe000" "100" "101" "L3:0=7f0;1=7f0;2=7f0;3=7f0"
