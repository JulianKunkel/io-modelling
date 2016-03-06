#!/bin/bash

THREADS=20
FILESIZE=$((1000000 / $THREADS))
FILE=test
TYPE=1TBparallel
MEM_SIZE=1024
REPEATS=$FILESIZE
PREALLOCATE=0
SEED=-1

mkdir out

for REPEAT in 1 2 3 ; do

for FILE_MODE in seq rnd; do

for ACCESS_MODE in "W" "R"; do

if [[ "$ACCESS_MODE" == "W" && "$ACCESS_MODE" != "rnd" ]] ; then
   TRUNCATE=1
   WAIT=0
else
   TRUNCATE=0
   WAIT=1
fi

for ACCESS_SIZE in 1048576 ; do

for MEM_MODE in off0 ; do

ls -lah test*

RUN="$MEM_SIZE $FILESIZE $REPEATS $TRUNCATE $ACCESS_SIZE $MEM_MODE $FILE_MODE $PREALLOCATE 1 0 $ACCESS_MODE $SEED $WAIT"
OUTFILE="out/${TYPE}-${FILE}_$(echo $RUN | sed "s/ /_/g")-$REPEAT"

if [[ ! -e "$OUTFILE-T1.txt" ]] ; then
   echo "Running $RUN"

   for T in $(seq 1 $THREADS) ; do

   (   
   ERR=0
   TOUTFILE="$OUTFILE-T${T}.txt"
   ( 
      date
      # lustre stats
      cat /proc/fs/lustre/llite/lustre01-*/read_ahead_stats || exit 1
      cat /proc/fs/lustre/llite/lustre01-*/stats || exit 1
      cat /proc/stat  || exit 1
      cat /proc/vmstat || exit 1

      echo ${FILE}-$T $(echo $RUN)
      ./io-model ${FILE}-$T $(echo $RUN) 
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
      )  > "$TOUTFILE.tmp" 
   if [[ "$ERR" == "0" ]] ; then      
      mv ${TOUTFILE}.tmp $TOUTFILE
   else
      echo "An error occured, output was:"
      cat ${TOUTFILE}.tmp
   fi
   ) & 

   done
   wait
fi

done


done

done

done

done
