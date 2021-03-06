#!/bin/bash

FILESIZE=10240
FILE=test
TYPE=cached
MEM_SIZE=1024
REPEATS=10000
PREALLOCATE=0
SEED=3

mkdir out

for REPEAT in 1 2 3 ; do

for ACCESS_MODE in "W" "WRITEAGAIN" "R"; do

if [[ "$ACCESS_MODE" == "W" ]] ; then
   TRUNCATE=1
   WAIT=0
else
   TRUNCATE=0
   WAIT=1
   # prefill cache:   
   dd if=/dev/zero of=test bs=1024k count=11000
   sync
   sleep 120
fi



for ACCESS_SIZE in $((4096*16)) $((4096*64)) $((4096*128)) 1048576 1 4 16 64 256 1024 4096 8192 $((4096*4))  2097152 4194304 8388608 16777216 ; do

for MEM_MODE in off0 seq rnd rnd8388608 stride8388608,8388608 reverse0; do

for FILE_MODE in off0 seq rnd rnd8388608 stride8388608,8388608 reverse0; do


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
