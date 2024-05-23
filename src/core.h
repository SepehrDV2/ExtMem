#ifndef CORE_H
#define CORE_H

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <signal.h>
#ifndef __cplusplus
#include <stdatomic.h>
#else
#include <atomic>
#define _Atomic(X) std::atomic< X >
#endif

#ifdef __cplusplus
extern "C" {
#endif
#ifdef PAGE_RANK
#include "policies/pageRank.h"  
#else 
#include "policies/disklinux.h"
#endif


#include "timer.h"
#include "intercept_layer.h"
#include "uthash.h"
#include "fifo.h"

#include "storagemanager.h"

//#define STATS_THREAD

#define MEM_BARRIER() __sync_synchronize()

#define CORE_PREFAULT_RATE  (128)
#define CORE_PREFETCH_RATE  (32)

//#define TIEREDMEM
#define SWAPPER

#define USWAP_IOURING
//#define USWAP_SPDK

#define USWAP_SIGNAL  
//#define USWAP_UFFD

//#define POPULATE_ALL
#define TRY_PREFAULT
#define TRY_PREFETCH

extern uint64_t dramsize;

#define DRAMSIZE_DEFAULT  (8L * (1024L * 1024L * 1024L))
#define DISKSIZE_DEFAULT  (32L * (1024L * 1024L * 1024L))


#define SWAPPATH_DEFAULT   "/dev/nvme1n1p2"

#define BASEPAGE_SIZE	  (4UL * 1024UL)
#define HUGEPAGE_SIZE 	(2UL * 1024UL * 1024UL)
#define GIGAPAGE_SIZE   (1024UL * 1024UL * 1024UL)
#define PAGE_SIZE 	    BASEPAGE_SIZE

#define BASEPAGE_MASK	(BASEPAGE_SIZE - 1)
#define HUGEPAGE_MASK	(HUGEPAGE_SIZE - 1)
#define GIGAPAGE_MASK   (GIGAPAGE_SIZE - 1)

#define BASE_PFN_MASK	(BASEPAGE_MASK ^ UINT64_MAX)
#define HUGE_PFN_MASK	(HUGEPAGE_MASK ^ UINT64_MAX)
#define GIGA_PFN_MASK   (GIGAPAGE_MASK ^ UINT64_MAX)

extern FILE *logfile;
//#define LOG(...) fprintf(stderr, __VA_ARGS__)
//#define LOG(...)	fprintf(logfile, __VA_ARGS__)
#define LOG(str, ...) while(0) {}

//#define LOGPOLICY(...) fprintf(stderr, __VA_ARGS__)
//#define LOGPOLICY(...)	fprintf(logfile, __VA_ARGS__)
#define LOGPOLICY(str, ...) while(0) {}

//#define LOGFLUSH(...)	fflush(__VA_ARGS__)
//#define LOGFLUSH(...)	fflush(logfile)
#define LOGFLUSH(...) while(0) {}


extern FILE *traces;
//#define LOG_TRACE(...)	fprintf(traces, __VA_ARGS__)
#define LOG_TRACE(...)	while(0) {}


//#define LOG_TIME(str, ...) log_time(str, __VA_ARGS__)
//#define LOG_TIME(str, ...) fprintf(timef, str, __VA_ARGS__)
#define LOG_TIME(str, ...) while(0) {}

extern FILE *statsf;
#define LOG_STATS(str, ...) fprintf(stderr, str,  __VA_ARGS__)
//#define LOG_STATS(str, ...) fprintf(statsf, str, __VA_ARGS__)
//#define LOG_STATS(str, ...) while (0) {}

#if defined (PAGE_RANK)
  #define pagefault(...) lrudisk_pagefault(__VA_ARGS__)
  #define paging_init(...) lrudisk_init(__VA_ARGS__)
  #define mmgr_remove(...) lrudisk_remove_page(__VA_ARGS__)
  #define mmgr_stats(...) lrudisk_stats(__VA_ARGS__)
  #define policy_shutdown(...) while(0) {}
  #define page_swapin(...) lrudisk_swapin(__VA_ARGS__)
  #define page_swapin_external(...) lrudisk_swapin_external(__VA_ARGS__)
  #define policy_add_page(...) lrudisk_track_page(__VA_ARGS__)
  #define policy_detach_page(...) lrudisk_detach_page(__VA_ARGS__)
  #define policy_put_freepage(...) lrudisk_put_page_free(__VA_ARGS__)
  #define page_swapin_external_async(...) lrudisk_swapin_external_async(__VA_ARGS__)
  #define policy_ack_swapped_in(...) lrudisk_ack_swapin(__VA_ARGS__)
  #define policy_ack_vma(...) lrudisk_ack_vma(__VA_ARGS__)
#elif defined (LRU_SWAP)
  #define pagefault(...) lrudisk_pagefault(__VA_ARGS__)
  #define paging_init(...) lrudisk_init(__VA_ARGS__)
  #define mmgr_remove(...) lrudisk_remove_page(__VA_ARGS__)
  #define mmgr_stats(...) lrudisk_stats(__VA_ARGS__)
  #define policy_shutdown(...) while(0) {}
  #define page_swapin(...) lrudisk_swapin(__VA_ARGS__)
  #define page_swapin_external(...) lrudisk_swapin_external(__VA_ARGS__)
  #define policy_add_page(...) lrudisk_track_page(__VA_ARGS__)
  #define policy_detach_page(...) lrudisk_detach_page(__VA_ARGS__)
  #define policy_put_freepage(...) lrudisk_put_page_free(__VA_ARGS__)
  #define page_swapin_external_async(...) lrudisk_swapin_external_async(__VA_ARGS__)
  #define policy_ack_swapped_in(...) lrudisk_ack_swapin(__VA_ARGS__)
  #define policy_ack_vma(...) lrudisk_ack_vma(__VA_ARGS__)
#endif

#define MAX_USER_THREADS (128)
#define MAX_UFFD_MSGS	    (1)
#define MAX_COPY_THREADS  (4)

extern uint64_t cr3;
extern bool is_init;
extern uint64_t missing_faults_handled;
extern uint64_t migrations_up;
extern uint64_t migrations_down;
extern __thread bool internal_malloc;
extern __thread bool old_internal_call;
extern __thread bool internal_call;
extern __thread bool internal_munmap;


enum pagetypes {
  HUGEP = 0,
  BASEP = 1,
  NPAGETYPES
};

struct user_page {
  uint64_t va;
  uint64_t virtual_offset;
  volatile bool in_dram;
  volatile bool swapped_out; 
  enum pagetypes pt;
  volatile bool migrating;
  volatile bool has_reserve;
  volatile bool present;
  uint64_t naccesses;
  struct user_page *reserve;
  UT_hash_handle hh;
  struct user_page *next, *prev;
  volatile struct fifo_list *list;
};

static inline uint64_t pt_to_pagesize(enum pagetypes pt)
{
  switch(pt) {
  case HUGEP: return HUGEPAGE_SIZE;
  case BASEP: return BASEPAGE_SIZE;
  default: assert(!"Unknown page type");
  }
}

static inline enum pagetypes pagesize_to_pt(uint64_t pagesize)
{
  switch (pagesize) {
    case BASEPAGE_SIZE: return BASEP;
    case HUGEPAGE_SIZE: return HUGEP;
    default: assert(!"Unknown page ssize");
  }
}

void extmem_init();
void extmem_stop();
void* extmem_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int extmem_munmap(void* addr, size_t length);
void *handle_fault();
void extmem_wp_page(struct user_page *page, bool protect);

void handle_first_fault(uint64_t page_boundry);
void handle_wp_fault(struct user_page *page);
void handle_missing_fault(struct user_page *page);

//void extmem_migrate_updisk(struct user_page *page, struct user_page *freepage);
void extmem_migrate_updisk(struct user_page *page, uint64_t dram_offset);
void extmem_migrate_downdisk(struct user_page *page, uint64_t disk_offset, bool need_wp, bool writeback);

void extmem_disk_write(int storage_fd, void* src, uint64_t dest, size_t pagesize);
void extmem_disk_read(int storage_fd, uint64_t src, void *dest, size_t pagesize);


struct user_page* get_user_page(uint64_t va);

void extmem_print_stats();
void extmem_clear_stats();


void uswap_sig_handler(int code, siginfo_t *siginfo, void *context);

void uswap_register_handler(int sig);

int core_try_prefault(uint64_t base_boundry);

#ifdef __cplusplus
}
#endif

#endif // CORE_H