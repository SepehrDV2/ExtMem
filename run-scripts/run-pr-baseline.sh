#!/bin/bash


CURRENT_DIR=$(pwd)

export DRAMSIZE=6442450944 # 6GB 
export SWAPDIR="/dev/nvme1n1p2" # swap path, change according to your system, note that this will be overwritten

# prepare the swap partition
swapoff -a
mkswap $SWAPDIR
swapon $SWAPDIR # just in case it was used in baseline

# create the cgroup limit
# Check if the cgroup already exists
if [ ! -d "/sys/fs/cgroup/memory/pagerank" ]; then
    # Create the cgroup if it doesn't exist
    cgcreate -g memory:/pagerank
    echo "Created new cgroup: memory/pagerank"
else
    echo "Using existing Cgroup memory/pagerank"
fi

echo $DRAMSIZE > /sys/fs/cgroup/memory/pagerank/memory.limit_in_bytes

echo "running without any memory limit (in-mem)"
../swap-benchmarks/gapbs/pr -f twitter_compressed/snap.el -i 10 -n 5

echo "running with Cgroups limit"
cgexec -g memory:pagerank ../swap-benchmarks/gapbs/pr -f twitter_compressed/snap.el -i 10 -n 5