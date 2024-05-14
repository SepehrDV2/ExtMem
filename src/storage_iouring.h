/** A storage manager based on Linux iouring. 
 * This backend highly leverages async IO. Must have async-IO capable device to use.
 * Can choose polling for lower latency
 */
#ifndef STORAGE_URING_H
#define STORAGE_URING_H

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>

void uswap_init_iouring(void);

void ioring_write_store(int fd, off_t offset, const void *data, size_t size);
void ioring_read_store(int fd, off_t offset, void *buffer, size_t size);
void thread_init_iouring();

void ioring_finish_read(int fd, off_t offset, void *buffer, size_t size);
void ioring_start_read(int fd, off_t offset, void *buffer, size_t size);

void ioring_prepare_read(int fd, off_t offset, void *buffer, size_t size);
void ioring_submit_all_reads();

#endif
