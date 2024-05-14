#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <libsyscall_intercept_hook_point.h>
#include <syscall.h>
#include <errno.h>
#define __USE_GNU
#include <dlfcn.h>
#include <pthread.h>
#include <sys/mman.h>
#include <assert.h>

#include "core.h"
#include "intercept_layer.h"

void* (*libc_mmap)(void *addr, size_t length, int prot, int flags, int fd, off_t offset) = NULL;
int (*libc_munmap)(void *addr, size_t length) = NULL;
void* (*libc_malloc)(size_t size) = NULL;
void (*libc_free)(void* ptr) = NULL;

static int mmap_filter(void *addr, size_t length, int prot, int flags, int fd, off_t offset, uint64_t *result)
{
  
  LOG("mmap interposed: 0x%lx, %ld\n", (uint64_t)addr, length);
  LOGFLUSH();
  
  // We don't cover file mappings in this version
  if (((flags & MAP_ANONYMOUS) != MAP_ANONYMOUS)) {
    LOG("syscall_intercept: calling native mmap due for file mapping: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    LOGFLUSH();
  
    return 1;
  }

  // stack mappings are better handled natively and we don't support them yet
  if ((flags & MAP_STACK) == MAP_STACK) {
    // pthread mmaps are called with MAP_STACK
    LOG("syscall_intercept: calling native mmap due to stack mapping: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    LOGFLUSH();
  
    return 1;
  }


  if (internal_call) {
    LOG("syscall_intercept: calling native mmap for internal extmem call: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    LOGFLUSH();
  
    return 1;
  }
  
  if (!is_init) {
    LOG("syscall_intercept: mmap called inside in init phase. calling native mmap\n");
    LOGFLUSH();
  
    return 1;
  }

  // bypassing the smaller mappings in this version
  if (length < 2UL * 1024UL * 1024UL * 1024UL) {
    LOG("syscall_intercept: calling native mmap for a small allocation: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    LOGFLUSH();
  
    return 1;
  }

  LOG("syscall_intercept: mmap redirected to extmem: mmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
  LOGFLUSH();
  if ((*result = (uint64_t)extmem_mmap(addr, length, prot, flags, fd, offset)) == (uint64_t)MAP_FAILED) {
    // TODO: handle extmem mmap fails gracefully
    LOG("extmem mmap failed\n\tmmap(0x%lx, %ld, %x, %x, %d, %ld)\n", (uint64_t)addr, length, prot, flags, fd, offset);
    LOGFLUSH();
  
  }
  return 0;
}


static int munmap_filter(void *addr, size_t length, uint64_t* result)
{
  
  
  if (internal_call) {
    return 1;
  }

  // munmap is still buggy, so filter all munmaps
  // just to be sure, TODO: fix munmap in core
  LOG("munmap intercepted: 0x%lx, %ld\n", (uint64_t)addr, length);
  LOGFLUSH();

  if ((*result = extmem_munmap(addr, length)) == -1) {
    LOG("extmem munmap failed\n\tmunmap(0x%lx, %ld)\n", (uint64_t)addr, length);
  }

  LOGFLUSH();
  return 0;
}


static void* bind_symbol(const char *sym)
{
  void *ptr;
  if ((ptr = dlsym(RTLD_NEXT, sym)) == NULL) {
    fprintf(stderr, "interception layer: dlsym failed (%s)\n", sym);
    abort();
  }
  //fprintf(stdout, "binding %s to %p", sym, ptr);
  LOG("binding %s to %p", sym, ptr);
  
  return ptr;
}


static int hook(long syscall_number, long arg0, long arg1, long arg2, long arg3,	long arg4, long arg5,	long *result)
{
	if (syscall_number == SYS_mmap) {
	  return mmap_filter((void*)arg0, (size_t)arg1, (int)arg2, (int)arg3, (int)arg4, (off_t)arg5, (uint64_t*)result);
	} else if (syscall_number == SYS_munmap){
    return munmap_filter((void*)arg0, (size_t)arg1, (uint64_t*)result);
  } else {
    // TODO: add madvise and read/write interception here too
    // ignore non-mmap system calls
		return 1;
	}
}

static __attribute__((constructor)) void init(void)
{
  libc_mmap = bind_symbol("mmap");
  libc_munmap = bind_symbol("munmap");
  libc_malloc = bind_symbol("malloc");
  libc_free = bind_symbol("free");
  intercept_hook_point = hook;

  extmem_init();
}

static __attribute__((destructor)) void extmem_close(void)
{
  extmem_stop();
}


