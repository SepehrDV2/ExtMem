#define _GNU_SOURCE
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <sys/mman.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>

#include "core.h"
#include "storage_iouring.h"
#include "liburing.h"

#define QD 128  // Number of queue entries (queue depth)

__thread struct io_uring io_ring;
__thread struct io_uring io_read_ring;
__thread bool uring_init = false;
__thread bool uring_read_init = false;

void uswap_init_iouring(){

    // poll mode makes difference
    //int ret = io_uring_queue_init(QD, &io_ring, IORING_SETUP_IOPOLL);
    int ret = io_uring_queue_init(QD, &io_ring, 0);
    if (ret < 0) {
        perror("io_uring_queue_init");
        assert(0);
    }

    uring_init = true;

}

void thread_init_iouring(){

    // keeping the interrupt mode (not polling)
    //int ret = io_uring_queue_init(QD, &io_ring, IORING_SETUP_IOPOLL);
    int ret = io_uring_queue_init(QD, &io_ring, 0);
    if (ret < 0) {
        perror("io_uring_queue_init");
        assert(0);
    }

    uring_init = true;

}

void thread_init_read_iouring(){

    // use polling for less critical path latency
    int ret = io_uring_queue_init(QD, &io_read_ring, IORING_SETUP_IOPOLL);
    //int ret = io_uring_queue_init(QD, &io_read_ring, 0);
    //int ret = io_uring_queue_init(QD, &io_ring, 0);
    if (ret < 0) {
        perror("io_uring_read_queue_init");
        assert(0);
    }

    uring_read_init = true;

}


void ioring_read_store(int fd, off_t offset, void *buffer, size_t size) {
    struct io_uring_sqe *sqe;

    // this may also leak memory for each thread
    if(uring_init == false){
        thread_init_iouring();
        uring_init = true;
    }
    sqe = io_uring_get_sqe(&io_ring);

    if (!sqe) {
        perror("io_uring_get_sqe");
        assert(0);
    }

    io_uring_prep_read(sqe, fd, buffer, size, offset);
    io_uring_sqe_set_data(sqe, buffer); // this will do the job of setting the copy back buffer

    if (io_uring_submit(&io_ring) < 0) {
        fprintf(stderr, "io_uring_submit");
        perror("io_uring_submit");
        assert(0);
    }

    struct io_uring_cqe *cqe;
    if (io_uring_wait_cqe(&io_ring, &cqe) < 0) {
        fprintf(stderr, "io_uring_wait_cqe");
        perror("io_uring_wait_cqe");
        assert(0);
    }

    io_uring_cqe_seen(&io_ring, cqe);
}


void ioring_write_store(int fd, off_t offset, const void *data, size_t size) {
    struct io_uring_sqe *sqe;
    
    // this may also leak memory for each thread
    if(uring_init == false){
        thread_init_iouring();
        uring_init = true;
    }
    sqe = io_uring_get_sqe(&io_ring);
    

    if (!sqe) {
        perror("io_uring_get_sqe");
        assert(0);
    }

    io_uring_prep_write(sqe, fd, data, size, offset);

    if (io_uring_submit(&io_ring) < 0) {
        fprintf(stderr, "io_uring_submit");
        perror("io_uring_submit");
        assert(0);
    }

    // Sync the disk, but we may not need this if O_DIRECT
    //fdatasync(fd);
    //LOG("io_uring submitted write\n");
    struct io_uring_cqe *cqe;
    if (io_uring_wait_cqe(&io_ring, &cqe) < 0) {
        fprintf(stderr, "io_uring_wait_cqe");
        perror("io_uring_wait_cqe");
        assert(0);
    }

    //LOG("io_uring received write\n");
    
    io_uring_cqe_seen(&io_ring, cqe);
}


void ioring_start_read(int fd, off_t offset, void *buffer, size_t size) {
    struct io_uring_sqe *sqe;

    // this may also leak memory for each thread, or it won't?
    if(uring_read_init == false){
        thread_init_read_iouring();
        uring_read_init = true;
    }
    sqe = io_uring_get_sqe(&io_read_ring);

    if (!sqe) {
        perror("io_uring_get_sqe");
        assert(0);
    }

    io_uring_prep_read(sqe, fd, buffer, size, offset);
    io_uring_sqe_set_data(sqe, buffer); // this should do the job of copying back into buffer

    if (io_uring_submit(&io_read_ring) < 0) {
        fprintf(stderr, "io_uring_submit");
        perror("io_uring_submit");
        assert(0);
    }

    // struct io_uring_cqe *cqe;
    // if (io_uring_wait_cqe(&io_ring, &cqe) < 0) {
    //     fprintf(stderr, "io_uring_wait_cqe");
    //     perror("io_uring_wait_cqe");
    //     assert(0);
    // }

    // io_uring_cqe_seen(&io_ring, cqe);
}

void ioring_prepare_read(int fd, off_t offset, void *buffer, size_t size) {
    struct io_uring_sqe *sqe;

    // this may also leak memory for each thread, or it won't?
    if(uring_read_init == false){
        thread_init_read_iouring();
        uring_read_init = true;
    }
    sqe = io_uring_get_sqe(&io_read_ring);

    if (!sqe) {
        perror("io_uring_get_sqe");
        assert(0);
    }

    io_uring_prep_read(sqe, fd, buffer, size, offset);
    //io_uring_sqe_set_data(sqe, buffer); // this should do the job of copying back into buffer

    //if (io_uring_submit(&io_read_ring) < 0) {
    //    fprintf(stderr, "io_uring_submit");
    //    perror("io_uring_submit");
    //    assert(0);
    //}

    // struct io_uring_cqe *cqe;
    // if (io_uring_wait_cqe(&io_ring, &cqe) < 0) {
    //     fprintf(stderr, "io_uring_wait_cqe");
    //     perror("io_uring_wait_cqe");
    //     assert(0);
    // }

    // io_uring_cqe_seen(&io_ring, cqe);
}

void ioring_submit_all_reads(){

    if (io_uring_submit(&io_read_ring) < 0) {
       fprintf(stderr, "io_uring_submit");
       perror("io_uring_submit");
       assert(0);
    }

}

void ioring_finish_read(int fd, off_t offset, void *buffer, size_t size) {

    assert(uring_read_init == true);

    struct io_uring_cqe *cqe;
    if (io_uring_wait_cqe(&io_read_ring, &cqe) < 0) {
        fprintf(stderr, "io_uring_wait_cqe");
        perror("io_uring_wait_cqe");
        assert(0);
    }

    io_uring_cqe_seen(&io_read_ring, cqe);
}


void ioring_prepare_write(int fd, off_t offset, const void *data, size_t size) {
    struct io_uring_sqe *sqe;
    
    // this way of initialization may also leak memory for each thread
    // TODO: Initialize properly
    if(uring_init == false){
        thread_init_iouring();
        uring_init = true;
    }
    sqe = io_uring_get_sqe(&io_ring);
    

    if (!sqe) {
        perror("io_uring_get_sqe");
        assert(0);
    }

    io_uring_prep_write(sqe, fd, data, size, offset);


}

void ioring_submit_all_writes(){


    if (io_uring_submit(&io_ring) < 0) {
        fprintf(stderr, "io_uring_submit");
        perror("io_uring_submit");
        assert(0);
    }

}


void ioring_finish_write(){
    // Sync the disk, may not need if O_DIRECT
    //fdatasync(fd);
    //LOG("io_uring submitted write\n");
    struct io_uring_cqe *cqe;
    if (io_uring_wait_cqe(&io_ring, &cqe) < 0) {
        fprintf(stderr, "io_uring_wait_cqe");
        perror("io_uring_wait_cqe");
        assert(0);
    }

    //LOG("io_uring received write\n");
    
    io_uring_cqe_seen(&io_ring, cqe);
}
