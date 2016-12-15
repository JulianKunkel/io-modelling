#!/bin/bash

export PATH=$PATH:/usr/lib64/mpich/bin/

FILESIZE=10240
FILE=/mnt/test/file
TYPE=cached
MEM_SIZE=1024
REPEATS=10000
PREALLOCATE=0
SEED=3
ECHO=

function runBenchmark(){
  NAME=$1
  DEVICE=$2
  ROOT_DEV=$3
  OPTIONS=$4

	for fs in ext4 xfs btrfs ; do

    mkdir -p $NAME/$fs

    if [[ "$fs" == "ext4" ]] ; then
    $ECHO mkfs.$fs $DEVICE || exit 1
    else
    $ECHO  mkfs.$fs -f $DEVICE || exit 1
    fi
    $ECHO  mount $DEVICE $OPTIONS /mnt/test || exit 1

  	for REPEAT in 1 2 3 ; do
    	for ACCESS_MODE in "W" "WRITEAGAIN" "R"; do

      	if [[ "$ACCESS_MODE" == "W" ]] ; then
      	   TRUNCATE=1
      	   WAIT=0
      	else
      	   TRUNCATE=0
      	   WAIT=1
      	   # prefill cache:
           for N in 1 2 3 4 5 6 7 8 10 12 ; do
      	      $ECHO  dd if=/dev/zero of=$FILE-$I bs=1024k count=11000
           done
      	   $ECHO sync
      	   $ECHO sleep 120
      	fi

      	for ACCESS_SIZE in $((4096*16)) $((4096*64)) $((4096*128)) 1048576 1 4 16 64 256 1024 4096 8192 $((4096*4))  2097152 4194304 8388608 16777216 ; do

        	for MEM_MODE in off0 seq rnd rnd8388608 stride8388608,8388608 reverse0; do

          	for FILE_MODE in off0 seq rnd rnd8388608 stride8388608,8388608 reverse0; do

            	for N in 1 2 3 4 5 6 7 8 10 12 ; do
              	RUN="$MEM_SIZE $FILESIZE $REPEATS $TRUNCATE $ACCESS_SIZE $MEM_MODE $FILE_MODE $PREALLOCATE 1 0 $ACCESS_MODE $SEED $WAIT"
                OUTDIR="$NAME/$fs/$N-${TYPE}-$(echo $RUN | sed "s/ /_/g")-$REPEAT"
                mkdir -p $OUTDIR
              	OUTFILE="$OUTDIR/output.txt"

              	if [[ ! -e "$OUTFILE" ]] ; then
                   RUN="$ECHO mpiexec -n $N $BENCH ./io-model $FILE $(echo $RUN) $ROOT_DEV"
                   echo "Running $RUN"
              	   ERR=0
              	   (
              	      date
              	      $RUN
              	      ERR="$?"
              	      date
              	      )  > "test.tmp"
              	   if [[ "$ERR" == "0" ]] ; then
              	      $ECHO mv test.tmp $OUTFILE
                      $ECHO mv *.csv $OUTDIR
                      echo ""
              	   else
              	      echo "An error occured"
              	      $ECHO cp test.tmp $OUTFILE.err
              	   fi
              	fi
              done
          	done
        	done
      	done
    	done
  	done
  	$ECHO umount /mnt/test || exit 1

	done
}

runBenchmark "HDD" "/dev/sdc1" "sdc" "-o relatime"
runBenchmark "SSD" "/dev/sdd1" "sdd" "-o discard,relatime"
