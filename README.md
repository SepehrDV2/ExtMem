
# ExtMem

ExtMem is a framework for user level memory management. ExtMem is implemented as a user-space library that attaches to applications. The current version supports memory paging (swap) and eviction and prefetching policies based on page table access bits. See the publication for details.

This repo contains our artifact for the ATC'24 paper. Follow the instructions in this doc to reproduce the paper results. 

### Publications
Jalalian, S., Patel, S., Rezaei Hajidehi, M., Seltzer, M., & Fedorova, A. (2024). ExtMem: Enabling Application-Aware virtual memory management. In  _Usenix Annual Technical Conference_ 2024.

‌
## Getting started

Clone this repository and set up its submodules. 

    git clone git@github.com:SepehrDV2/ExtMem.git
    cd ExtMem
    git submodule update --init


You need to build and install the provided custom kernel for the best results. Then build and install the kernel inside Linux directory. ExtMem also needs the custom kernel headers. Follow any Linux installation guide for dependencies.

    cd linux
    make oldconfig # assuming X86
    make menuconfig # ensure userfaultfd and uring are enabled
    make -j $(nproc)
    sudo make headers_install INSTALL_HDR_PATH=usr/include # important, we put headers in the source file
    # next steps install the actual kernel on your machine
    sudo make install 
    sudo make INSTALL_MOD_STRIP=1 modules_install 
    # update your grub and reboot

ExtMem uses libsyscall_intercept to redirect memory related system calls. Build and install this library from source:
https://github.com/pmem/syscall_intercept

Build ExtMem in the src directory: 

    make libextmem-default.so # make default policy
    make all # all policies

You can link ExtMem to any application. Pass the swap file directory as environment variable. It is recommended to give a async IO capable SSD storage partition. Set the DRAM limit of the application running ExtMem with environment variable. Don't limit an ExtMem-based application with Cgroups.

    DRAMSIZE=size_in_bytes SWAPDIR=/swap/device/path LD_PRELOAD=/path/to/extmem/src/libextmem-default.so ./your-executable

As an example run the random microbenchmark with 8GB of memory.

    cd swap-microbenchmarks/mmapbench
    g++ -O3 -g mmapbench.cpp -o mmapbench -ltbb -pthread # build the microbenchmark
    DRAMSIZE=8589934592 SWAPDIR=/dev/nvme1n1p2 LD_PRELOAD=/path/to/libextmem-default.so timeout 180s  ./mmapbench  /dev/null  1  1  0  0  1
    


## Kernel
The kernel is based on Linux 5.15 with modifications in userfaultfd and signal handler. Build following any Linux installation guide.

## Microbenchmarks
We use [mmapbench](https://github.com/SepehrDV2/mmap-anon-benchmarks/tree/extmem-eval) for microbenchmarks. Refer to its repository for running documentation. 

    cd swap-microbenchmarks/mmapbench
    g++ -O3 -g mmapbench.cpp -o mmapbench -ltbb -pthread # build the microbenchmark

Use scripts in swap-microbenchmarks/eval-scripts for evaluation. 
## Benchmarks
Currently we have tested ExtMem with [GAP](https://github.com/SepehrDV2/gapbs) benchmark suite for graph processing. 

    # building the gap benchmark
    cd swap-benchmarks/gapbs
    make
    make test
    
Prepare the twitter dataset.

Running PageRank:

    DRAMSIZE=8589934592 SWAPPATH=/dev/nvme1n1p2 LD_PRELOAD=/path/to/libextmem-pagerank.so ./pr -f ../datasets/twitter/snap.el



## Acknowledgment
Some parts of this implementation were derived from [hemem](https://bitbucket.org/ajaustin/hemem/src). We have used the high level structure, syscall interception code and also some utilities from their codebase. We also adopted some of their kernel patches for userfaultfd interface in our kernel tree. 
 
## Contact
Sepehr Jalalian (Sepehr.jalalian.edu@gmail.com)

