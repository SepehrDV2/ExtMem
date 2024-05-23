#!/bin/bash


CURRENT_DIR=$(pwd)

export DEFAULTLIB_SO="$CURRENT_DIR/../src/libextmem-default.so"
export SPECIALLIB_SO="$CURRENT_DIR/../src/libextmem-pagerank.so"
export DRAMSIZE=5768709120 # About 5.5GB  to account for metadata and smaller arrays (baseline has 6GB)
export SWAPDIR="/dev/nvme1n1p2" # swap path, change according to your system, note that this will be overwritten

swapoff -a # just in case it was used in baseline

echo "running with the default policy"
LD_PRELOAD=$DEFAULTLIB_SO ../swap-benchmarks/gapbs/pr -f twitter_compressed/snap.el -i 10 -n 2

echo "running with the specialized policy"
LD_PRELOAD=$SPECIALLIB_SO ../swap-benchmarks/gapbs/pr -f twitter_compressed/snap.el -i 10 -n 5
