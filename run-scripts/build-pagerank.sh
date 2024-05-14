#!/bin/bash

# build the gapbs
cd ../swap-benchmarks/gapbs
make clean
make 
make test

echo "make sure to prepare the twitter dataset via prepare-tw-data.sh before running the run-pr.sh"