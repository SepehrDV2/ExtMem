#!/bin/bash


CURRENT_DIR=$(pwd)

export MYLIB_SO="$CURRENT_DIR/../src/libextmem-default.so"
export DRAMSIZE=8289934592 # less than 8GB to account for metadata
export SWAPDIR="/dev/nvme1n1p2" # swap path, change according to your system, note that this will be overwritten

swapoff $SWAPDIR # just in case it was used in baseline
# Define the range of num_threads
start_threads=1
end_threads=16


# run random update 
for ((threads=start_threads; threads<=end_threads; threads*=2)); do
  # Run the mmapbench command and redirect the output to a log file
  LD_PRELOAD=$MYLIB_SO timeout 180s ../swap-microbenchmarks/mmapbench/mmapbench /dev/null "$threads" 1 0 0 1 > "extmem-rand-write-$threads-threads.log"
done


# run sequential update
# Loop through num_threads in powers of 2
for ((threads=start_threads; threads<=end_threads; threads*=2)); do
  # Run the mmapbench command and redirect the output to a log file
  LD_PRELOAD=$MYLIB_SO timeout 180s ../swap-microbenchmarks/mmapbench/mmapbench /dev/null "$threads" 0 0 0 1 > "extmem-seq-write-$threads-threads.log"
done

# run working-set microbenchmark, only for 8 threads

LD_PRELOAD=$MYLIB_SO timeout 180s ../swap-microbenchmarks/mmapbench/mmapbench /dev/null 8 2 0 0 1 > "extmem-workingset-write-8-threads.log"