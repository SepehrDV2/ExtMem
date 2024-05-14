#!/bin/bash

cd ../swap-microbenchmarks/mmapbench
g++ -O3 -g mmapbench.cpp -o mmapbench -ltbb -pthread

