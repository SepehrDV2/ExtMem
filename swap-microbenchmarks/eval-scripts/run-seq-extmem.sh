#!/bin/bash

# Define the range of num_threads
start_threads=1
end_threads=32

# Loop through num_threads in powers of 2
for ((threads=start_threads; threads<=end_threads; threads*=2)); do
  # Run the mmapbench command and redirect the output to a log file
  LD_PRELOAD=/home/sepehr/uswap-dev/5.15-Uswap/src/libuswap-disklrulinux.so timeout 180s ./mmapbench /dev/null "$threads" 0 0 0 0 > "extmem-seq-read-$threads-8-16.log"
done





