#!/bin/bash


CURRENT_DIR=$(pwd)

export DRAMSIZE=8589934592 
export SWAPDIR="/dev/nvme1n1p2" # swap path, change according to your system, note that this will be overwritten

# prepare the swap partition
swapoff -a
mkswap $SWAPDIR
swapon $SWAPDIR # just in case it was used in baseline

# create the cgroup limit
# Check if the cgroup already exists
if [ ! -d "/sys/fs/cgroup/memory/mmapbench" ]; then
    # Create the cgroup if it doesn't exist
    cgcreate -g memory:/mmapbench
    echo "Created new cgroup: memory/mmapbench"
else
    echo "Using existing Cgroup memory/mmapbench"
fi

#sudo cgcreate -g memory:/mmapbench
echo $DRAMSIZE > /sys/fs/cgroup/memory/mmapbench/memory.limit_in_bytes

# Define the range of num_threads
start_threads=1
end_threads=32

# random update 
for ((threads=start_threads; threads<=end_threads; threads*=2)); do
  # Run the mmapbench command and redirect the output to a log file
  cgexec -g memory:mmapbench timeout 180s ../swap-microbenchmarks/mmapbench/mmapbench /dev/null "$threads" 1 0 0 1 > "cgroup-ran-write-$threads-threads.log"
done

# sequential update         
for ((threads=start_threads; threads<=end_threads; threads*=2)); do
  # Run the mmapbench command and redirect the output to a log file
  cgexec -g memory:mmapbench timeout 180s ../swap-microbenchmarks/mmapbench/mmapbench /dev/null "$threads" 1 0 0 1 > "cgroup-seq-write-$threads-threads.log"
done


# working-set microbenchmark (only 8 threads)
cgexec -g memory:mmapbench timeout 180s ../swap-microbenchmarks/mmapbench/mmapbench /dev/null 8 2 0 0 1 > "cgroup-workingset-write-8-threads.log"