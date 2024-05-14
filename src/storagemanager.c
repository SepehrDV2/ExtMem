/** Simple storage manager for reading and writing pages to disk
 * Later can replace this with more sophisticated implementations
 or nvme over SPDK for high performance
*/
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
#include <stdio.h>
#include <string.h>

#include "core.h"
#include "storagemanager.h"





/*int init_storage_manager(int diskfd, StorageManager* sm) {
    sm->file = fopen(filename, "r+");
    if (sm->file == NULL) {
        // file doesn't exist, create it
        sm->file = fopen(filename, "w+");
        if (sm->file == NULL) {
            return 0;
        }
        sm->num_pages = 0;
    } else {
        // file already exists, get number of pages
        fseek(sm->file, 0, SEEK_END);
        sm->num_pages = ftell(sm->file) / PAGE_SIZE;
        fseek(sm->file, 0, SEEK_SET);
    }
    return 1;
}*/


void read_page(int diskfd, uint64_t file_offset, char* dest, size_t pagesize) {
    

    if(pread(diskfd, dest, pagesize, file_offset) == -1){
        perror("disk read error");
        assert(0);
    }

    return;

}

void write_page(int diskfd, uint64_t file_offset, void* src, size_t pagesize) {
    

    
    if(pwrite(diskfd, src, pagesize, file_offset) == -1){
        perror("disk write error ar ar");
    }

    //if(fsync(diskfd) == -1){
    //    perror("fsync error");
    //}

    return;
}

/*void close_storage_manager(StorageManager* sm) {
    fclose(sm->file);
    sm->file = NULL;
    sm->num_pages = 0;
}*/
