#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>

#include "../core.h"
#include "pageRank.h"
#include "../timer.h"
#include "../fifo.h"
#include "../observability.h"



static struct fifo_list active_list;
static struct fifo_list inactive_list;
static struct fifo_list disk_active_list;
static struct fifo_list disk_written_list;
static struct fifo_list dram_free_list;
static struct fifo_list disk_free_list;
static struct fifo_list disk_reserve_list;
static struct fifo_list eviction_list;

static pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t policy_lock = PTHREAD_MUTEX_INITIALIZER;

static bool __thread in_kswapd = false;
uint64_t lru_runs = 0;
static volatile bool in_kscand = false;
volatile bool kswapd_running = false;
extern uint64_t main_mmap;

volatile bool shrinkage_running = false;
pthread_t kswapd_thread;
pthread_t scan_thread;
pthread_t eviction_worker[WRITER_THREADS];

pthread_t prefetch_worker[WRITER_THREADS];
volatile uint64_t safe_to_free = 0;

uint64_t pagerank_block_size = PR_BLOCK_SIZE;
uint64_t pagerank_window_size = PR_WINDOW_SIZE;

volatile struct uswap_vma main_vma;

extern pthread_mutex_t allocator_lock;
extern pthread_mutex_t handler_lock;


static pthread_mutex_t kswapd_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t kswapd_cv = PTHREAD_COND_INITIALIZER;

extern uint64_t disksize;


static void lrudisk_migrate_down(struct user_page *page, uint64_t offset, bool need_wp, bool writeback)
{
  struct timeval start, end;

  gettimeofday(&start, NULL);

  page->migrating = true;
  // don't wp again if it is done somewhere else
  if(need_wp){
    extmem_wp_page(page, true);
  }
  extmem_migrate_downdisk(page, offset, 0, writeback);
  //page->migrating = false; // don't release this now
  // we want the page to go into a list first

  gettimeofday(&end, NULL);
  LOG_TIME("migrate_down: %f s\n", elapsed(&start, &end));
}


static void lrudisk_migrate_up(struct user_page *page, uint64_t offset)
{
  struct timeval start, end;

  gettimeofday(&start, NULL);

  //page->migrating = true;
  //extmem_wp_page(page, true);
  extmem_migrate_updisk(page, offset);
  //page->migrating = false;
  //page->swapped_out = false;
  gettimeofday(&end, NULL);
  LOG_TIME("migrate_up: %f s\n", elapsed(&start, &end));
}

void lrudisk_swapin(struct user_page *page, struct user_page *freepage)
{
  struct timeval start, end;
  uint64_t offset = freepage->virtual_offset;
  uint64_t file_offset = page->virtual_offset;
  
  //pthread_mutex_lock(&global_lock);

  assert(freepage->in_dram == true);
  assert((freepage->list == &active_list)); // this does not look good
  page_list_remove_page(&active_list, freepage);
  assert(page->list == &disk_active_list);
  page_list_remove_page(&disk_active_list, page);
  offset = freepage->virtual_offset;

  lrudisk_migrate_up(page, offset);

  // now that the new page is in memory release the page in disk
  freepage->virtual_offset = file_offset;
  freepage->in_dram = false;
  freepage->swapped_out = true;
  freepage->migrating = false;
  
  enqueue_fifo(&active_list, page);
  enqueue_fifo(&disk_free_list, freepage);
  
  //pthread_mutex_unlock(&global_lock);
  
}


void lrudisk_swapin_external(struct user_page *page)
{
  struct timeval start, end;
  struct user_page *freepage;
  uint64_t offset;
  uint64_t file_offset = page->virtual_offset;
  struct fifo_list *list;
  int ret = 0;
  //pthread_mutex_lock(&global_lock);
  //freepage = fetch_free_page();
  //gettimeofday(&start, NULL);

  safe_to_free = page->va;
  freepage = fetch_free_asynch();
  //gettimeofday(&end, NULL);


  //fprintf(stderr, "swapin: fetch free took %f s", elapsed(&start, &end));
    
  assert(freepage->in_dram == true);
  //assert((freepage->list == &active_list)); // this does not look good
  //page_list_remove_page(&active_list, freepage);
  do{ // disk page might move depending on policy
    list = page->list;
    assert(list == &disk_active_list); // for now
    ret = page_list_tryremove_page(&disk_active_list, page);
    assert(ret == 0);
  
  }while(ret != 0);
  offset = freepage->virtual_offset;
  lrudisk_migrate_up(page, offset);

  // now that the new page is in memory release the page in disk
  freepage->virtual_offset = file_offset;
  freepage->in_dram = false;
  freepage->swapped_out = true;
  freepage->migrating = false;
  
  
  // keeping the disk data as a reserve backing
  //enqueue_fifo(&disk_free_list, freepage);
  freepage->reserve = page;
  page->reserve = freepage;
  page->has_reserve = true;
  freepage->has_reserve = true;
  enqueue_fifo(&disk_reserve_list, freepage);
  enqueue_fifo(&inactive_list, page);
  //pthread_mutex_unlock(&global_lock);
  
}

void lrudisk_swapin_external_async(struct user_page *page)
{
  struct timeval start, end;
  struct user_page *freepage;
  uint64_t offset;
  uint64_t file_offset = page->virtual_offset;
  struct fifo_list *list;
  int ret = 0;
  
  // detach the page first
  do{ // disk page might move depending on policy
    list = page->list;
    assert(list == &disk_active_list); // for now
    ret = page_list_tryremove_page(&disk_active_list, page);
    assert(ret == 0);
  
  }while(ret != 0);
  
  safe_to_free = page->va;
  
  core_migrate_up_async_start(page);
  
  
  freepage = fetch_free_asynch();
  

  
  assert(freepage->in_dram == true);
  //assert((freepage->list == &active_list)); // this does not look good
  //page_list_remove_page(&active_list, freepage);
  offset = freepage->virtual_offset;
  //lrudisk_migrate_up(page, offset);
  //gettimeofday(&start, NULL);

  core_migrate_up_async_finish(page, offset);

  //gettimeofday(&end, NULL);

  //fprintf(stderr, "migrate up async: finish took %f s\n", elapsed(&start, &end));
  
  // now that the new page is in memory release the page in disk
  freepage->virtual_offset = file_offset;
  freepage->in_dram = false;
  freepage->swapped_out = true;
  freepage->migrating = false;
  
  
  // keeping the disk data as a reserve backing
  //enqueue_fifo(&disk_free_list, freepage);
  freepage->reserve = page;
  page->reserve = freepage;
  page->has_reserve = true;
  freepage->has_reserve = true;
  enqueue_fifo(&disk_reserve_list, freepage);
  enqueue_fifo(&inactive_list, page);
  //pthread_mutex_unlock(&global_lock);
  
}

void lrudisk_ack_swapin(struct user_page *page, struct user_page *freepage)
{
  struct timeval start, end;
  uint64_t offset = freepage->virtual_offset;
  uint64_t file_offset = page->virtual_offset;
  
  //pthread_mutex_lock(&global_lock);

  assert(freepage->in_dram == true);
  assert((freepage->list == NULL));
  assert(page->swapped_out == true);
  assert(page->migrating == true);
  assert(page->list == NULL);
  
  // This function is called when we are sure transfer is completed
  freepage->virtual_offset = file_offset;
  freepage->in_dram = false;
  freepage->swapped_out = true;
  freepage->migrating = false;
  page->virtual_offset = offset;
  
  // keeping the disk data as a reserve backing
  //enqueue_fifo(&disk_free_list, freepage);
  freepage->reserve = page;
  page->reserve = freepage;
  page->has_reserve = true;
  freepage->has_reserve = true;
  enqueue_fifo(&disk_reserve_list, freepage);
  enqueue_fifo(&inactive_list, page);
  
  //pthread_mutex_unlock(&global_lock);
  
}

void lrudisk_ack_vma(void* vma_boundry, uint64_t vma_size, int priority)
{
  
  LOGPOLICY("new vma recorded, vma_size: %lU\n", vma_size);

  main_vma.va = vma_boundry;
  main_vma.size = vma_size;
  main_vma.priority = priority;

  return;
}


static int shrink_active_list(uint64_t nr_pages)
{
  //size_t nr_pages = active->numentries;
  struct user_page *page;
  uint64_t bits;
  uint64_t ret = 0, tries = 0;
  if(nr_pages == 0)
    return 0; 
  
  shrinkage_running = true;
  if(nr_pages > active_list.numentries)
    nr_pages = active_list.numentries;

  // find cold pages and move to inactive list
  while (ret < nr_pages && active_list.numentries > 0 && tries < nr_pages * 100) {
    page = dequeue_fifo(&active_list);
    if (page != NULL) {
      
      assert(page != NULL);
      // sample presentation method
      page->naccesses = 0;
      //extmem_clear_accessed_flag(page);
      enqueue_fifo(&inactive_list, page);
      //LOGPOLICY("shrink_active_list: page %lu examined and moved to inactive list\n", (uint64_t)(page->va - main_mmap)/ PAGE_SIZE);
      ret++;

      //
      tries++;
      

    }
    
  }
  shrinkage_running = false;
  return ret;
}

static int shrink_inactive_list(uint64_t nr_pages)
{
  //size_t nr_pages = active->numentries;
  struct user_page *page;
  uint64_t bits;
  uint64_t ret = 0, tries = 0;
  if(nr_pages == 0 || nr_pages > inactive_list.numentries)
    nr_pages = inactive_list.numentries;

  shrinkage_running = true;
  // find cold pages and move to inactive list
  if(safe_to_free == 0){
    while (ret < nr_pages && inactive_list.numentries > 0 && tries < nr_pages * 100) {
      page = dequeue_fifo(&inactive_list);
      if (page != NULL) {
        
        assert(page != NULL);

        bits = pt_get_bits(page);
        //bits = 0;
        //if(bits){
        //  migrations_up++;
        //}
        //pt_clear_bits(page);
          
        if ((bits & PT_ACCESSED_FLAG) == PT_ACCESSED_FLAG) {
          
          // page has been accessed,
          // put it back
          page->naccesses++;
          extmem_clear_accessed_flag(page);

          if(page->naccesses > 1){
            page->naccesses = 0;
            enqueue_fifo(&active_list, page);
            //LOGPOLICY("shrink_inactive_list: page %lu examined and moved to active list\n", (uint64_t)(page->va - main_mmap)/ PAGE_SIZE);

          }
          else{
            enqueue_fifo(&inactive_list, page);
            //page->naccesses = 0;
            //LOGPOLICY("shrink_inactive_list: page %lu examined and kept in inactive list\n", (uint64_t)(page->va - main_mmap)/ PAGE_SIZE);
          }
          //continue;
          
        }
        else {
          
            page->naccesses = 0;
            enqueue_fifo(&eviction_list, page);
            //LOGPOLICY("shrink_inactive_list: page %lu examined and moved to eviction list\n", (uint64_t)(page->va - main_mmap)/ PAGE_SIZE);
            ret++;
          
        }
        tries++;
        

      }
      
    }
  } else {  // we have safe to free
     while (ret < nr_pages && inactive_list.numentries > 0 && tries < nr_pages * 100) {
      page = dequeue_fifo(&inactive_list);
      if (page != NULL) {
        
        assert(page != NULL);
        
        // main condition for being safe to free, 2MB safety buffer
        //if((page->va <safe_to_free && safe_to_free - page->va < pagerank_window_size - (512UL * 4096UL)) ||
        //    (page->va > safe_to_free && page->va - safe_to_free > pagerank_window_size + (512UL * 4096UL))){
        if(!((page->va < safe_to_free && safe_to_free - page->va < (32UL * 4096UL * 4096UL)) ||
            (page->va > safe_to_free && page->va - safe_to_free < pagerank_window_size + (512UL * 4096UL)))){
        
              //page->naccesses = 0;
              enqueue_fifo(&eviction_list, page);
              //LOGPOLICY("shrink_inactive_list: page %lu examined and moved to eviction list\n", (uint64_t)(page->va - main_mmap)/ PAGE_SIZE);
              ret++;
        

            }
            else{
              enqueue_fifo(&inactive_list, page);
              
            }
        tries++;
        

      }
      
    }


  }
  shrinkage_running = false;
  return ret;
}


void *lrudisk_synchkswapd()
{
  int tries;
  struct user_page *p;
  struct user_page *cp;
  struct user_page *np;
  struct timeval start, end;
  uint64_t migrated_bytes;
  bool from_written_list = true;
  uint64_t old_offset;
  uint64_t ret;
  int nr_pages = 16;
  int to_cool = 32;
  int priority = 12;
  int nr_reclaimed = 0;
  //free(malloc(65536));
  
  in_kswapd = true;

  LOGPOLICY("Low water threshold is: %lu\n", (LOW_WATER_THREASHOLD * dramsize / (PAGE_SIZE * 100)));
  for (;;) {
    //usleep(KSWAPD_INTERVAL);

    if((int64_t)(active_list.numentries + inactive_list.numentries) <= (LOW_WATER_THREASHOLD * dramsize / (PAGE_SIZE * 100))){
      LOGPOLICY("Low threshold reached, kswapd going back to sleep\n");
      priority = 12;
      nr_reclaimed = 0;
      pthread_mutex_lock(&kswapd_lock);
      pthread_cond_wait(&kswapd_cv, &kswapd_lock); // only wake up when needed
      pthread_mutex_unlock(&kswapd_lock);
    
    }
    kswapd_running = true;
    //pthread_mutex_lock(&global_lock);

    // calculate the number of pages to reclaim
    nr_pages = (int64_t)(active_list.numentries + inactive_list.numentries) - ((int64_t)(LOW_WATER_THREASHOLD * dramsize / (PAGE_SIZE * 100)));
    if(nr_pages <= 0){  // no need to evict
      kswapd_running = false;
      //LOGPOLICY("kswapd: no page to reclaim, nr=%lu, active: %lu, inactive: %lu\n", nr_pages, active_list.numentries, inactive_list.numentries); 
      continue;
    }
    gettimeofday(&start, NULL);
    
    nr_reclaimed = direct_allocate_page_asynch(nr_pages, priority);

    //calculate number of pages to reclaim
    // update lists
    gettimeofday(&end, NULL);
    
 
    // For now we don't bring back swapped out pages unless there is a demand
    // this should not affect current scenarios
    // but in future algorithms it is worth considering

out:
    kswapd_running = false;
    //lru_runs++;
    //pthread_mutex_unlock(&global_lock);
    //LOG_TIME("migrate: %f s\n", elapsed(&start, &end));
  }

  return NULL;
}

void *lrudisk_prefetchworker()
{
  int tries;
  uint64_t prefetch_head;
  uint64_t block_head;
  int worker_id = 1;
  int nr_prefetched = 0;
  //int success = 0;
  internal_call = true; // forever
  usleep(1000000);
    
  for (;;) {
    // not started yet
    //fprintf(stderr,"prefetch worker main_vma: %lu, safe_to_free: %lu, block_head: %lu, prefetch head %lu\n", main_vma.va, safe_to_free, block_head, prefetch_head );
    
    if(main_vma.va == 0 || safe_to_free == 0 || main_vma.size < (5UL * 1024UL * 1024UL * 1024UL) ){
      continue;
    } 
      
    // no change since last iteration?
    if(block_head == safe_to_free + (pagerank_block_size * worker_id)){
      continue;
    } 
    
    block_head = safe_to_free + (pagerank_block_size * worker_id);
    prefetch_head = block_head;
    while(prefetch_head <= block_head + (pagerank_block_size)){
    
      assert(prefetch_head % PAGE_SIZE == 0);
      //fprintf(stderr,"core try prefetch for prefetch head %lx\n", (void*)prefetch_head );
      nr_prefetched = core_try_prefetch(prefetch_head);
      //fprintf(stderr,"core try prefetch, successfuly prefetched %d pages for prefetch head %lx\n", nr_prefetched, (void*)prefetch_head );
      
      if(nr_prefetched == 0){ // there could be a problem
        usleep(100);
        break;
      }

      prefetch_head = prefetch_head + (nr_prefetched * PAGE_SIZE);

    }
      
   
    
    }

  return NULL;
}


void *lrudisk_evictionworker()
{
  int tries;
  struct user_page *victim;
  struct user_page *diskpage;
  uint64_t flags;
  bool dirty;
  bool writeback = true;
  struct fifo_list *list;
  //struct user_page *np;
  struct timeval start, end;
  uint64_t migrated_bytes;
  bool from_written_list = true;
  uint64_t old_offset;
  int ret = 0;

  internal_call = true; // forever
  
  for (;;) {
    victim = dequeue_fifo(&eviction_list);
    if(victim == NULL){
      //frpintf(stderr, "found no eviction page, going to sleep\n");
      usleep(EVICTOR_INTERVAL);
      continue;
    }
      
    assert(victim != NULL);
    assert(victim->va != 0);
    //assert(0);
    // critical phase, write protect and check the dirty bit
    // then do the eviction
    LOGPOLICY("Eviction worker: going to swap out page %lx\n", victim->va);
      
    victim->migrating = true;
    
    extmem_wp_page(victim, true);

    
    
    flags = pt_get_bits(victim);
    
    
    dirty = flags & PT_DIRTY_FLAG;
    
    //pt_clear_bits(victim);
    writeback = dirty | (!(victim->has_reserve));  // check again
    //extmem_tlb_shootdown(victim->va);
    
    LOGPOLICY("Eviction worker: read page flags: %lu, firty bit is %d, writeback is %d\n", flags, dirty, writeback);
    
    
    // if the page is already swap backed
    if(victim->has_reserve){
      diskpage = victim->reserve;
      assert(diskpage != NULL);
      assert(diskpage->reserve == victim);
      
      do{ // disk page might move depending on policy
        list = diskpage->list;
        assert(list == &disk_reserve_list); // for now
        ret = page_list_tryremove_page(&disk_reserve_list, diskpage);
        assert(ret == 0);
      
      }while(ret != 0);

      old_offset = victim->virtual_offset;
      
      lrudisk_migrate_down(victim, diskpage->virtual_offset, 0, writeback);

      
      diskpage->virtual_offset = old_offset;
      diskpage->in_dram = true;
      diskpage->present = false;
      //diskpage->hot = true;
      diskpage->has_reserve = false;
      diskpage->reserve = NULL;
      victim->has_reserve = false;
      victim->reserve = NULL;
      enqueue_fifo(&dram_free_list, diskpage);
      enqueue_fifo(&disk_active_list, victim);
      
      assert(victim->migrating);
      assert(victim->swapped_out == true);
      victim->migrating = false;
      victim->swapped_out = true;
      //pthread_mutex_unlock(&handler_lock);
        
        
    }
    else{ // first time evicting or reserve deleted, allocate in disk
      LOGPOLICY("Eviction worker: victim does not have reserve\n");
    
      diskpage = dequeue_fifo(&disk_free_list);
      if (diskpage != NULL) {
        assert(!(diskpage->present));

        old_offset = victim->virtual_offset;
        //lrudisk_migrate_down(page, diskpage->virtual_offset, dirty);
        //gettimeofday(&start, NULL);
    
        lrudisk_migrate_down(victim, diskpage->virtual_offset, 0, writeback);
        //gettimeofday(&end, NULL);
    
        //fprintf(stderr, "evictionworker: writback time: %f s\n", elapsed(&start, &end));

        enqueue_fifo(&disk_active_list, victim);
        diskpage->virtual_offset = old_offset;
        diskpage->in_dram = true;
        diskpage->present = false;


        //enqueue_fifo(&active_list, diskpage);
        enqueue_fifo(&dram_free_list, diskpage);
        
        assert(victim->migrating);
        assert(victim->swapped_out == true);
        victim->migrating = false;
        victim->swapped_out = true;
        //pthread_mutex_unlock(&handler_lock);
        
        assert(diskpage != NULL);
        
      }
      else{
        assert(!"no disk page found");
      }
    }
    
    
    }

  return NULL;
}


void *lrudisk_evictionworker_vector()
{
  int tries;
  struct user_page *victim[EVICT_VECTOR_SIZE];
  struct user_page *diskpage[EVICT_VECTOR_SIZE];
  uint64_t flags;
  bool dirty[EVICT_VECTOR_SIZE];
  bool writeback[EVICT_VECTOR_SIZE];
  struct fifo_list *list;
  //struct user_page *np;
  struct timeval start, end;
  uint64_t migrated_bytes;
  bool from_written_list = true;
  uint64_t old_offset[EVICT_VECTOR_SIZE];
  int ret = 0;
  int nr_evicting = 0; 

  internal_call = true; // forever
  
  for (;;) {
 
    nr_evicting = dequeue_fifo_vector(&eviction_list, victim, EVICT_VECTOR_SIZE);
    if(nr_evicting == 0){  // no pages to evict?
      //fprintf(stderr, "found no eviction page!\n");
      //fflush(stderr);
      usleep(EVICTOR_INTERVAL);
      continue;
    }

    assert(victim[0] != NULL);
    //gettimeofday(&start, NULL);
    //LOGPOLICY("found %d eviction pages!\n", nr_evicting);
    int nr_prepared = 0;
    while(nr_prepared < nr_evicting){
      assert(victim[nr_prepared]->va != 0);
    
      victim[nr_prepared]->migrating = true;
      
      old_offset[nr_prepared] = victim[nr_prepared]->virtual_offset;

      extmem_wp_page(victim[nr_prepared], true);

      flags = pt_get_bits(victim[nr_prepared]);
      
      dirty[nr_prepared] = flags & PT_DIRTY_FLAG;

      writeback[nr_prepared] = dirty[nr_prepared] | (!(victim[nr_prepared]->has_reserve));  // AA

      nr_prepared++;
    }

    
    // get the needed disk pages
    nr_prepared = 0;
    while(nr_prepared < nr_evicting){
    
      // if the page is already swap backed
    if(victim[nr_prepared]->has_reserve){
      diskpage[nr_prepared] = victim[nr_prepared]->reserve;
      assert(diskpage[nr_prepared] != NULL);
      assert(diskpage[nr_prepared]->reserve == victim[nr_prepared]);
      
      do{ // disk page might move depending on policy
        list = diskpage[nr_prepared]->list;
        assert(list == &disk_reserve_list); // for now
        ret = page_list_tryremove_page(&disk_reserve_list, diskpage[nr_prepared]);
        assert(ret == 0);
      
      }while(ret != 0);

      //old_offset = victim->virtual_offset;
      
      //lrudisk_migrate_down(victim, diskpage->virtual_offset, 0, writeback);
        
        
    }
    else{ // first time evicting or reserve deleted, allocate in disk
      LOGPOLICY("Eviction worker: victim does not have reserve\n");
    
      diskpage[nr_prepared] = dequeue_fifo(&disk_free_list);
      if (diskpage[nr_prepared] != NULL) {
        assert(!(diskpage[nr_prepared]->present));

        //old_offset = victim->virtual_offset;
        //lrudisk_migrate_down(page, diskpage->virtual_offset, dirty);
        
        
      }
      else{
        assert(!"no disk page found");
      }
    }

      nr_prepared++;
    }

    //gettimeofday(&start, NULL);
    
    // at this point we should do the writes (if needed) and then madvise
    // assume vector size is small enough to be passed on stack
    extmem_migrate_downdisk_vector(nr_evicting, victim, diskpage, writeback);
    // track the pages
    //gettimeofday(&end, NULL);
    
    //fprintf(stderr, "evictionworker: writeback and madvise took: %f s for %d pages\n", elapsed(&start, &end), nr_evicting);

    nr_prepared = 0;
    while(nr_prepared < nr_evicting){
    
      // if the page is already swap backed
      if(victim[nr_prepared]->has_reserve){
        
        diskpage[nr_prepared]->virtual_offset = old_offset[nr_prepared];
        diskpage[nr_prepared]->in_dram = true;
        diskpage[nr_prepared]->present = false;
        //diskpage->hot = true;
        diskpage[nr_prepared]->has_reserve = false;
        diskpage[nr_prepared]->reserve = NULL;
        victim[nr_prepared]->has_reserve = false;
        victim[nr_prepared]->reserve = NULL;
        enqueue_fifo(&dram_free_list, diskpage[nr_prepared]);
        enqueue_fifo(&disk_active_list, victim[nr_prepared]);
        
        assert(victim[nr_prepared]->migrating);
        assert(victim[nr_prepared]->swapped_out == true);
        victim[nr_prepared]->migrating = false;
        victim[nr_prepared]->swapped_out = true;
        //pthread_mutex_unlock(&handler_lock);
        
          
      }
      else{ // first time evicting or reserve deleted, allocate in disk
        //LOGPOLICY("Eviction worker: victim does not have reserve\n");
      
          assert(!(diskpage[nr_prepared]->present));

          //old_offset = victim->virtual_offset;
          //lrudisk_migrate_down(page, diskpage->virtual_offset, dirty);
          
          enqueue_fifo(&disk_active_list, victim[nr_prepared]);
          diskpage[nr_prepared]->virtual_offset = old_offset[nr_prepared];
          diskpage[nr_prepared]->in_dram = true;
          diskpage[nr_prepared]->present = false;

          //enqueue_fifo(&active_list, diskpage);
          enqueue_fifo(&dram_free_list, diskpage[nr_prepared]);

          assert(victim[nr_prepared]->migrating);
          assert(victim[nr_prepared]->swapped_out == true);
          victim[nr_prepared]->migrating = false;
          victim[nr_prepared]->swapped_out = true;
          //pthread_mutex_unlock(&handler_lock);
          
          assert(diskpage[nr_prepared] != NULL);
          
          
      }

      nr_prepared++;
    }

    // done?
    assert(nr_prepared == nr_evicting);
    
    
//out:
    
    }

  return NULL;
}


/*  put some pages in evictor list */
int direct_allocate_page_asynch(int nr_pages, int priority)
{
  struct timeval start, end;
  struct user_page *page;
  struct user_page *diskpage;
  uint64_t old_offset;
  struct user_page *cp;
  int tries = 0;
  int ret;
  int reclaimed = 0, cooled = 0;
  int nr_scanned_active = 0, nr_scanned_inactive = 0;
  //int nr_to_scan;
  //if(nr_pages == 0){
  //  nr_pages = 10;
  //}
  
  int to_scan_active = active_list.numentries >> priority;
  int to_scan_inactive = inactive_list.numentries >> priority;
  // heuristic to keep the active two-third of the inactive
  while(nr_scanned_inactive < to_scan_inactive && reclaimed < nr_pages){
    
    reclaimed += shrink_inactive_list(SWAP_CLUSTER_COUNT);

    
    //LOGPOLICY("direct_allocate_asynch reclaim. nr=%lu Pages put on eviction list: %lu\n", nr_pages, ret);
    nr_scanned_inactive += SWAP_CLUSTER_COUNT;
    #if 0
    if(nr_scanned_active < to_scan_active){
      //gettimeofday(&start, NULL);

      cooled += shrink_active_list(SWAP_CLUSTER_COUNT);
      //gettimeofday(&end, NULL);
      //fprintf(stderr, "shrink_inactive_list: %f s\n", elapsed(&start, &end));

      //LOGPOLICY("direct_allocate_asynch shrunk active list with nr=%lu. Pages demoted: %lu\n", to_cool, ret);
      nr_scanned_active += SWAP_CLUSTER_COUNT;
    }
    #endif
    
  }
  
  //if(inactive_is_low()){
  //
  //}
  //LOGPOLICY("direct reclaim finished, pages cooled: %d , pages freed: %d, total tries: %d \n", cooled, reclaimed, tries);
  //assert(!"Out of memory");
  return reclaimed;
}


/*  called with global lock held via lru_pagefault function */
static struct user_page* lrudisk_allocate_page_critical()
{
  struct timeval start, end;
  struct user_page *page;
  struct user_page *cp;
  int tries;

  gettimeofday(&start, NULL);
  //for (tries = 0; tries < 2; tries++) {
    page = dequeue_fifo(&dram_free_list);
    if (page != NULL) {
      //pthread_mutex_lock(&(page->page_lock));
      assert(page->in_dram);
      //assert(!page->present);

      //page->present = true;
      enqueue_fifo(&inactive_list, page);
      //TODO: put in inactive list and set the accessed bit true
      gettimeofday(&end, NULL);
      LOG_TIME("mem_policy_allocate_page: %f s\n", elapsed(&start, &end));

      //pthread_mutex_unlock(&(page->page_lock));
      // if(((dram_free_list.numentries * 100) / (active_list.numentries + inactive_list.numentries + 1)) < KSWAPD_LOW_THRESHOLD){
      //   pthread_mutex_lock(&kswapd_lock);
      //   pthread_cond_signal(&kswapd_cv);
      //   pthread_mutex_unlock(&kswapd_lock);
      // }

      return page;
    }
    
    // DRAM was full, get a free page by force
    page = direct_allocate_page();
    if(page == NULL){
      perror("direct reclaim failed");
      assert(!"Out of memory");
    }

    // if(dram_free_list.numentries < KSWAPD_LOW_THRESHOLD){
    //   pthread_mutex_lock(&kswapd_lock);
    //   pthread_cond_signal(&kswapd_cv);
    //   pthread_mutex_unlock(&kswapd_lock);

    // }
    enqueue_fifo(&inactive_list, page);
      
    return page;
   
}

/*  Get free physical space, a listless in_dram page */
static struct user_page* fetch_free_asynch()
{
  struct timeval start, end;
  struct user_page *page = NULL;
  struct user_page *diskpage = NULL;
  struct fifo_list *list;
  uint64_t old_offset;
  uint64_t flags;
  int ret = 0;
  bool writeback = true;
  int priority = 12;  
  struct user_page *cp;
  int tries = 0;
  int nr_reclaimed = 0;
  int nr_pages = SWAP_CLUSTER_COUNT;
  //int nr_to_scan;

  if((kswapd_running == false && (uint64_t)(active_list.numentries + inactive_list.numentries) > (HIGH_WATER_THRESHOLD * dramsize / (PAGE_SIZE * 100)))){
    LOGPOLICY("High threshold reached waking up kswapd\n");
    pthread_mutex_lock(&kswapd_lock);
    pthread_cond_signal(&kswapd_cv);
    pthread_mutex_unlock(&kswapd_lock);
  }


  while(page == NULL && priority > 0){ 

    page = dequeue_fifo(&dram_free_list);
    if (page != NULL) {
   
      return page;
    }
    // DRAM was full, try to free pages
    //if(tries < 3 && (active_list.numentries + inactive_list.numentries > dramsize / (PAGE_SIZE * 2))){
    //  direct_allocate_page_asynch(nr_pages);
    //}
    //tries++;
    if(shrinkage_running == false){
      if((active_list.numentries + inactive_list.numentries > dramsize / (PAGE_SIZE * 2))){
      //if((active_list.numentries + inactive_list.numentries > dramsize / (PAGE_SIZE * 2)) && priority > 0){
        nr_reclaimed += direct_allocate_page_asynch(nr_pages, priority);
        priority--;  
      }
    }

  //   usleep(1);
  // // try again now
  // page = dequeue_fifo(&dram_free_list);
  //   if (page != NULL) {
  //   return page;
  // }

  // now try to evict one page by yourself again
  // TODO: make optimized decision
  #if 1
  page = dequeue_fifo(&eviction_list);
  if (page != NULL) {
    
    assert(page != NULL);
    
    page->migrating = true;
    
    extmem_wp_page(page, true);

    flags = pt_get_bits(page);
    bool dirty = flags & PT_DIRTY_FLAG;
    //pt_clear_bits(page); // no need to clear as we are going to zap this
    writeback = dirty | (!(page->has_reserve));
    // find a free disk page to move the cold dram page to
    
    if(page->has_reserve){
      diskpage = page->reserve;
      assert(diskpage != NULL);
      assert(diskpage->reserve == page);
      
      do{ // disk page might move depending on policy
        list = diskpage->list;
        assert(list == &disk_reserve_list); // for now
        ret = page_list_tryremove_page(&disk_reserve_list, diskpage);
        assert(ret == 0);
      
      }while(ret != 0);

      old_offset = page->virtual_offset;
      
      lrudisk_migrate_down(page, diskpage->virtual_offset, 0, writeback);
        
      diskpage->virtual_offset = old_offset;
      diskpage->in_dram = true;
      diskpage->present = false;
      //diskpage->hot = true;
      diskpage->has_reserve = false;
      diskpage->reserve = NULL;
      page->has_reserve = false;
      page->reserve = NULL;
      //enqueue_fifo(&dram_free_list, diskpage);
      enqueue_fifo(&disk_active_list, page);
      
      assert(page->migrating);
      assert(page->swapped_out == true);
      page->migrating = false;
      page->swapped_out = true;
      //pthread_mutex_unlock(&handler_lock);
      
      assert(diskpage != NULL);
      return diskpage;
        
    }
    else{
      diskpage = dequeue_fifo(&disk_free_list);
      if (diskpage != NULL) {
        assert(!(diskpage->present));

        old_offset = page->virtual_offset;
        lrudisk_migrate_down(page, diskpage->virtual_offset, 1, writeback);
        assert(page->swapped_out);
        enqueue_fifo(&disk_active_list, page);
        diskpage->virtual_offset = old_offset;
        diskpage->in_dram = true;
        diskpage->present = false;
        //diskpage->hot = false;
        //page->hot = false;
        page->migrating = false;

        //enqueue_fifo(&active_list, diskpage);
        assert(diskpage != NULL);
        return diskpage;
      }
      else{
        assert(!"No disk page found");
      }
    }
  }
  #endif


    //return page;
    //priority--;
  }
  assert(!"Out of memory");
}


struct user_page* lrudisk_pagefault(void)
{
  struct user_page *page;

  page = fetch_free_asynch();
  //pthread_mutex_unlock(&global_lock);
  assert(page != NULL);
  page->migrating = true; // to protect it from being paged out
  //enqueue_fifo(&inactive_list, page);
  return page;
}

void lrudisk_track_page(struct user_page *page)
{
  assert(page->list == NULL);
  enqueue_fifo(&inactive_list, page);
  return;
}

void lrudisk_put_page_free(struct user_page *page)
{
  assert(page->list == NULL);
  assert(page->va == 0);
  enqueue_fifo(&dram_free_list, page);
  return;
}

void lrudisk_detach_page(struct user_page *page)
{
  struct fifo_list *list;
  int ret = 0;
  
  //assert(page->list == NULL);
  //assert(page->va == 0);
  
  do{ // disk page might move depending on policy
    list = page->list;
    assert(list == &disk_active_list); // for now
    ret = page_list_tryremove_page(&disk_active_list, page);
    assert(ret == 0);
  
  }while(ret != 0);
  
  return;
}

void lrudisk_remove_page(volatile struct user_page *page)
{
  volatile struct fifo_list *list;
  int ret = 0;
  // wait for kscand thread to complete its scan
  // this is needed to avoid race conditions with kscand thread
  //while (in_kscand);
  
  //pthread_mutex_lock(&policy_lock);
 
  // if the page is being swapped in, then the 
  // user program must have had a read-after-free problem.
  // if it is being swapped out, then nobody should be swapping it in
  assert(page != NULL);
  assert(page->va != 0);
  //while(page->migrating == true);

  // racy part, use locks to solve this later
  //page->migrating = true;
  //pthread_mutex_lock(&(page->page_lock));
  
  //LOGPOLICY("LRU: remove page: va: 0x%lx\n", page->va);
  
  // we need to remove this page from any list it is in
  // the page might actually be on air, then we would wait
  // until it's placed somewhere
  do{
    ret = 0;
    list = page->list;
    //assert(list != NULL);
    //if(list == &eviction_list || list == NULL){  // race condition with eviction workers
    if(list == &eviction_list){  // race condition with eviction workers
      //assert(0);
      usleep(10);
      ret = 1;
      continue;
    }
    ret = page_list_tryremove_page(list, page);
  }while(ret != 0);
  
  page->present = false;
  page->va = 0; 
  
  if (page->in_dram) {
    // must remove the swap backing as well
    if(page->has_reserve){
      struct user_page *reserve_page = page->reserve;
      assert(reserve_page != NULL);
      assert(reserve_page->reserve == page);
      do{
        list = reserve_page->list;
        assert(list == &disk_reserve_list);
        ret = page_list_tryremove_page(list, reserve_page);
      }while(ret != 0);
      reserve_page->has_reserve = false;
      reserve_page->reserve = NULL;
      reserve_page->present = false;
      enqueue_fifo(&disk_free_list, reserve_page);
      page->has_reserve = false;
      page->reserve = NULL;
    }
    enqueue_fifo(&dram_free_list, page);
  }
  else if(page->swapped_out){
    //assert(page->reserve == NULL);  // TODO: fix this
    page->reserve = NULL; // wrong,
    page->has_reserve = false;
    enqueue_fifo(&disk_free_list, page);

  }
  else {
    assert(!"no other configuration");
  
  }

  //page->migrating = false;
  //pthread_mutex_unlock(&(page->page_lock));
  //pthread_mutex_unlock(&global_lock);
}


void lrudisk_init(void)
{
  int r;
  LOG("lru_init: started\n");

  pthread_mutex_init(&(dram_free_list.list_lock), NULL);
  for (int i = 0; i < dramsize / PAGE_SIZE; i++) {
    struct user_page *p = calloc(1, sizeof(struct user_page));
    p->virtual_offset = i * PAGE_SIZE;
    p->present = false;
    p->in_dram = true;
    p->swapped_out = false;
    p->pt = pagesize_to_pt(PAGE_SIZE);
    p->has_reserve = false;
    p->reserve = NULL;
    
    enqueue_fifo(&dram_free_list, p);
  }

  pthread_mutex_init(&(disk_free_list.list_lock), NULL);
  for (int i = 0; i < disksize / PAGE_SIZE; i++) {
    struct user_page *p = calloc(1, sizeof(struct user_page));
    p->virtual_offset = i * PAGE_SIZE;
    p->present = false;
    p->in_dram = false;
    p->swapped_out = true;
    p->pt = pagesize_to_pt(PAGE_SIZE);
    p->has_reserve = false;
    p->reserve = NULL;
    

    enqueue_fifo(&disk_free_list, p);
  }

  //pthread_mutex_init(&(active_list.list_lock), NULL);
  pthread_mutex_init(&(inactive_list.list_lock), NULL);
  
  pthread_mutex_init(&(disk_active_list.list_lock), NULL);
  
  pthread_mutex_init(&(disk_reserve_list.list_lock), NULL);
  pthread_mutex_init(&(eviction_list.list_lock), NULL);

  pthread_mutex_init(&kswapd_lock, NULL);
  pthread_cond_init (&kswapd_cv, NULL);
  
  r = pthread_create(&kswapd_thread, NULL, lrudisk_synchkswapd, NULL);
  assert(r == 0);
  for(int i = 0; i < WRITER_THREADS; i++){
    #ifdef VECTOR_EVICTION
      r = pthread_create(&eviction_worker[i], NULL, lrudisk_evictionworker_vector, NULL);
    #else
      r = pthread_create(&eviction_worker[i], NULL, lrudisk_evictionworker, NULL);
    #endif
    assert(r == 0);
  }

  for(int i = 0; i < PREFETCHER_THREADS; i++){
    
    r = pthread_create(&prefetch_worker[i], NULL, lrudisk_prefetchworker, NULL);
    
    assert(r == 0);
  }



  LOG("PageRank special policy initialized\n");

}

void uswap_stats()
{

  LOG_STATS("Free DRAM pages: [%lu]\t Free disk pages: [%lu]\t Disk_entries: [%lu]\n", 
            dram_free_list.numentries,
            disk_free_list.numentries,
            disk_active_list.numentries);
   
}

