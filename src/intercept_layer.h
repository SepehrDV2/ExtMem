#ifndef INTERCEPT_LAYER_H
#define INTERCEPT_LAYER_H

#include <stdlib.h>

// replacing libc functions in our namespace
extern void* (*libc_mmap)(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
extern int (*libc_munmap)(void *addr, size_t length);
extern void* (*libc_malloc)(size_t size);
extern void (*libc_free)(void* p);

#endif

