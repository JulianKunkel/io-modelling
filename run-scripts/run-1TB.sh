#!/bin/bash

FILESIZE=1000000
FILE=test
TYPE=1TB
MEM_SIZE=1024
REPEATS=1000000
PREALLOCATE=0
SEED=3

mkdir out

for REPEAT in 1 2 3 ; do

for FILE_MODE in seq; do

for ACCESS_MODE in "W" "R"; do

if [[ "$ACCESS_MODE" == "W" ]] ; then
   TRUNCATE=1
   WAIT=0
else
   TRUNCATE=0
   WAIT=1
fi



for ACCESS_SIZE in 1048576 ; do

for MEM_MODE in off0 ; do


RUN="$FILE $MEM_SIZE $FILESIZE $REPEATS $TRUNCATE $ACCESS_SIZE $MEM_MODE $FILE_MODE $PREALLOCATE 1 0 $ACCESS_MODE $SEED $WAIT"
OUTFILE="out/${TYPE}-$(echo $RUN | sed "s/ /_/g")-$REPEAT.txt"

if [[ ! -e "$OUTFILE" ]] ; then
   echo "Running $RUN"
   
   ERR=0
   ( 
      date
      # lustre stats
      cat /proc/fs/lustre/llite/lustre01-*/read_ahead_stats || exit 1
      cat /proc/fs/lustre/llite/lustre01-*/stats || exit 1
      cat /proc/stat  || exit 1
      cat /proc/vmstat || exit 1

      ./io-model $(echo $RUN) 
      ERR="$?"

      cat /proc/fs/lustre/llite/lustre01-*/read_ahead_stats
      cat /proc/fs/lustre/llite/lustre01-*/stats   
      cat /proc/stat 
      cat /proc/vmstat 
      echo "standard information"
      hostname
      cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq
      lfs getstripe $FILE
      date
      )  > "$OUTFILE.tmp" 
   if [[ "$ERR" == "0" ]] ; then      
      mv ${OUTFILE}.tmp $OUTFILE
   else
      echo "An error occured, output was:"
      cat ${OUTFILE}.tmp
   fi
fi

done


done

done

done

done
