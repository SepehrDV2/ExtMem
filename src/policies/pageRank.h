#ifndef PAGERANK_H
#define PAGERANK_H

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>

#include "../core.h"


#define HIGH_WATER_THRESHOLD (90)
#define LOW_WATER_THREASHOLD (80)
#define INACTIVE_RATE (40)

#define EVICTOR_INTERVAL   (2) // in us 
#define WRITER_THREADS  (12)
#define EVICT_VECTOR_SIZE (8)
//#define VECTOR_EVICTION

#define PREFETCHER_THREADS  (1)

// Hyperparameters for CSR policy
#define PR_WINDOW_SIZE (128UL  * 1024UL * 1024UL) // 512MB
#define PR_BLOCK_SIZE (32UL  * 1024UL * 1024UL) // 128MB
#define PR_PREFETCH_RATE (1024) // 1024 pages


#define SWAP_CLUSTER_COUNT  (32)
#define KSWAPD_RECLAIM_RATE (4) // default number of pages to reclaim each time
#define KSWAPD_LOW_THRESHOLD (10)
void *lrudisk_kswapd();
struct user_page* lrudisk_pagefault(void);
struct user_page* lrudisk_pagefault_unlocked(void);
void lrudisk_init(void);
void lrudisk_remove_page(volatile struct user_page *page);
void lrudisk_stats();
static struct user_page* fetch_free_page();
static struct user_page* fetch_free_asynch();
int direct_allocate_page_async(int nr_pages, int priority);
void uswap_stats();
void lrudisk_track_page(struct user_page *page);
void lrudisk_put_page_free(struct user_page *page);
void *lrudisk_evictionworker_vector();
void *lrudisk_evictionworker();


void lrudisk_swapin(struct user_page *page, struct user_page *freepage);

struct uswap_vma {
  uint64_t va;
  int priority;
  uint64_t size;
};

#endif
