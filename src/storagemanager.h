/** Simple storage manager for reading and writing pages to disk
 * Later can replace this with more sophisticated implementations
 or nvme over SPDK for high performance
*/
#ifndef STORAGEMANAGER_H
#define STORAGEMANAGER_H

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>


/*typedef struct {
    int fd;
    int num_pages;
    int filesize;
} StorageManager;*/

void write_page(int diskfd, uint64_t file_offset, void* src, size_t pagesize);

//void close_storage_manager(StorageManager* sm);

void read_page(int diskfd, uint64_t file_offset, char* dest, size_t pagesize);


#endif