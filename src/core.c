#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <sched.h>
#include <signal.h>

#include "core.h"
#include "timer.h"
#include "uthash.h"
#include "observability.h"

#ifdef USWAP_IOURING
#include "storage_iouring.h"
#else
#include "storagemanager.h"
#endif


pthread_t fault_thread;

uint64_t dramsize = 0;
uint64_t disksize = 0;


char* diskpath = NULL;


int diskfd = -2;
long uffd = -2;

bool is_init = false;
bool timing = false;

uint64_t mem_mmaped = 0;
uint64_t mem_allocated = 0;
uint64_t pages_allocated = 0;
uint64_t pages_freed = 0;
uint64_t fastmem_allocated = 0;
uint64_t slowmem_allocated = 0;
uint64_t diskmem_allocated = 0;
uint64_t wp_faults_handled = 0;
uint64_t first_faults_handled = 0;
uint64_t major_faults_handled = 0;
uint64_t migrations_up = 0;
uint64_t migrations_down = 0;
uint64_t disk_eviction = 0;
uint64_t disk_promotion = 0;
uint64_t bytes_migrated = 0;
uint64_t migration_waits = 0;

uint64_t main_mmap = 0;

FILE* logfile;
FILE* statsf;
FILE* timef;
FILE* traces;

pthread_t stats_thread;

struct user_page *pages = NULL;
pthread_mutex_t pages_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t allocator_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t handler_lock = PTHREAD_MUTEX_INITIALIZER;

void *disk_mmap;

__thread void *buffer_space;
void *page_cache;

__thread void *prefetch_buffer;

__thread bool internal_call = false;
__thread bool old_internal_call = false;

__thread bool buffer_allocated = false;
__thread bool prefetch_buffer_allocated = false;

__thread uint64_t prev_fault_2 = 0;
__thread uint64_t prev_fault_1 = 0;

volatile uint64_t prev_fault_global_2 = 0;
volatile uint64_t prev_fault_global_1 = 0;


static void *extmem_stats_thread()
{
  
  for (;;) {
    sleep(3);
    
    extmem_print_stats();
    //extmem_clear_stats();
  }
  return NULL;
}

void add_page(struct user_page *page)
{
  struct user_page *p;
  pthread_mutex_lock(&pages_lock);
  HASH_FIND(hh, pages, &(page->va), sizeof(uint64_t), p);
  assert(p == NULL);
  HASH_ADD(hh, pages, va, sizeof(uint64_t), page);
  pthread_mutex_unlock(&pages_lock);
}

void remove_page(struct user_page *page)
{
  pthread_mutex_lock(&pages_lock);
  HASH_DEL(pages, page);
  pthread_mutex_unlock(&pages_lock);
}

struct user_page* find_page(uint64_t va)
{
  struct user_page *page;
  pthread_mutex_lock(&pages_lock);
  HASH_FIND(hh, pages, &va, sizeof(uint64_t), page);
  pthread_mutex_unlock(&pages_lock);
  return page;
}

  
// In a real runtime, this would have to do some extra work to determine whether
// this is a "real" SIGBUS or just a userfault and handle it
// accordingly. We're skipping all that stuff here since we assume the userfaults are the only
// source of SIGBUS.
void uswap_sig_handler(int code, siginfo_t *siginfo, void *context)
{

  //LOG("Signal received\n");
  static struct uffd_msg msg[MAX_UFFD_MSGS];
  ssize_t nread;
  uint64_t fault_addr;
  uint64_t page_boundry;
  struct timeval start, end;
  
  struct user_page *page;
  int ret;
  int nmsgs;
  
    size_t addr;
    int page_number;
  
    
    //if ((code != SIGBUS)) {
    //    perror("non-SIGBUS signal received!");
    //}
    
    // fetch faulting address
    fault_addr = (uint64_t)siginfo->si_addr;
    
    // self-paging: determine our tid
    pid_t mytid = gettid();
      
    internal_call = true;
    // allign faulting address to page boundry
    // huge page boundry if applicable
    page_boundry = fault_addr & ~(PAGE_SIZE - 1);
    //LOG("Received SIGBUS, address: %lx, page: %lx, the thread %d is handling\n", fault_addr, page_boundry, (int)mytid);
    //LOGFLUSH();
    if ((code == SIGSEGV)) {
        LOG("Received SIGSEGV, address: %lx, page: %lx\n", fault_addr, page_boundry);
        assert(0);
        //perror("non-SIGBUS signal received!");
    }
    
    
    page = find_page(page_boundry);
    // this must be sub-optimal but with correctness guarantee
    gettimeofday(&start, NULL);
    
    if(page == NULL){ // page is not recorded so the first touch case
        handle_first_fault(page_boundry);
        //handle_first_fault_dummy(page_boundry);
          
    }
    else if(page->swapped_out == true){ // major fault
        handle_missing_fault(page);
    }
    else{  // write-protection fault
        handle_wp_fault(page);
    }
   
    internal_call = false;
    //LOG("Finished Signal handler for address %lx, page %lx in thread %d\n", fault_addr, page_boundry, (int)mytid);
    //LOGFLUSH();
    

    //gettimeofday(&end, NULL);
    //fprintf(stderr, "signal dummy fault after: %f s\n", elapsed(&start, &end));
  
    // After this we should go to kernel signal return path

}



void uswap_register_handler(int sig)
{
    struct sigaction action;
    LOG("Registering a signal handler\n");

    action.sa_flags = SA_RESTART;
    action.sa_handler = NULL;
    action.sa_sigaction = uswap_sig_handler;
    action.sa_flags |= SA_SIGINFO;
    sigemptyset(&action.sa_mask);

    int ret = sigaction(sig, &action, NULL);
    if (ret != 0)
    {
        perror("sigaction failed \n");
        
    }

    LOG("Signal handler for signal %d registered\n", sig);
}


void extmem_init()
{
  struct uffdio_api uffdio_api;
  int s = 1;

  internal_call = true;
/*
  internal calls are dangerous. We should not touch
  any user-program global state, including file descriptors
  and stdio buffers.
*/
  
  logfile = fopen("logs.txt", "w+");
  if (logfile == NULL) {
    perror("log file open\n");
    assert(0);
  }

  traces = fopen("traces.txt", "w+");
  if (traces == NULL) {
    perror("log file open\n");
    assert(0);
  }

  LOG("extmem_init: started\n");
  fprintf(logfile, "extmem_init: started\n" );

  fflush(logfile);



#ifdef SWAPPER
  // SSD Init
  // for now only requirement from disk is to have a file descriptor
  diskpath = getenv("SWAPDIR");
  if(diskpath == NULL) {
    diskpath = malloc(sizeof(SWAPPATH_DEFAULT));
    strcpy(diskpath, SWAPPATH_DEFAULT);
  }  

  diskfd = open(diskpath, O_RDWR | O_DIRECT);
  if (diskfd < 0) {
    perror("disk open");
  }
  assert(diskfd >= 0);
#endif
  

  uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK );
  if (uffd == -1) {
    perror("uffd");
    assert(0);
  }

  uffdio_api.api = UFFD_API;
  #ifdef USWAP_SIGNAL
  uffdio_api.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP |  UFFD_FEATURE_MISSING_SHMEM | UFFD_FEATURE_MISSING_HUGETLBFS | UFFD_FEATURE_SIGBUS;// | UFFD_FEATURE_EVENT_UNMAP | UFFD_FEATURE_EVENT_REMOVE;
  #else
  uffdio_api.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP |  UFFD_FEATURE_MISSING_SHMEM | UFFD_FEATURE_MISSING_HUGETLBFS;// | UFFD_FEATURE_EVENT_UNMAP | UFFD_FEATURE_EVENT_REMOVE;
  #endif
  uffdio_api.ioctls = 0;
  if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1) {
    perror("ioctl uffdio_api");
    assert(0);
  }

#ifdef USWAP_IOURING
  // Initialize IO_URING
  uswap_init_iouring();
#endif

  
  #ifdef USWAP_SIGNAL  // for self-paging mode
  uswap_register_handler(SIGBUS);
  //uswap_register_handler(SIGSEGV);  // depending on system may need sigsegv or sigbus
  #else  // IPC mode
  s = pthread_create(&fault_thread, NULL, handle_fault, 0);
  if (s != 0) {
    perror("pthread_create");
    assert(0);
  }
  #endif
  
  timef = fopen("times.txt", "w+");
  if (timef == NULL) {
    perror("time file fopen\n");
    assert(0);
  }

  statsf = fopen("stats.txt", "w+");
  if (statsf == NULL) {
    perror("stats file fopen\n");
    assert(0);
  }

  char* dramsize_string = getenv("DRAMSIZE");
  if(dramsize_string != NULL)
    dramsize = strtoull(dramsize_string, NULL, 10);
  else
    dramsize = DRAMSIZE_DEFAULT;

  
  /*int buffer_space_size = PAGE_SIZE * MAX_USER_THREADS;
  buffer_space =libc_mmap(NULL, buffer_space_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE , -1, 0);
  if (buffer_space == MAP_FAILED) {
    perror("buffer space mmap");
    assert(0);
  }*/
  

  //LOG("Buffer space mapped at %lx with size %lu\n", (uint64_t)buffer_space, (uint64_t)buffer_space_size );
  
  
  #ifdef SWAPPER
  // SSD mapping
  char* disksize_string = getenv("DISKSIZE");
  if(disksize_string != NULL)
    disksize = strtoull(disksize_string, NULL, 10);
  else
    disksize = DISKSIZE_DEFAULT;

  // We don't mmap the swap file anymore and it is not a requirement
  // disk_mmap =libc_mmap(NULL, disksize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, diskfd, 0);
  // if (disk_mmap == MAP_FAILED) {
  //   perror("disk mmap");
  //   assert(0);
  // }
  #endif

  // Page cache: small victim cache for swapped out pages
  page_cache =libc_mmap(NULL,16 * PAGE_SIZE * MAX_USER_THREADS, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE , -1, 0);
  if (page_cache == MAP_FAILED) {
    perror("page cache mmap");
    assert(0);
  }

  memset(page_cache , 0, PAGE_SIZE * 16 * MAX_USER_THREADS);


#ifdef STATS_THREAD
  s = pthread_create(&stats_thread, NULL, extmem_stats_thread, NULL);
  assert(s == 0);
#endif

  paging_init();

  is_init = true;

  struct user_page *dummy_page = calloc(1, sizeof(struct user_page));
  add_page(dummy_page);

  LOG("extmem_init: finished\n");

  internal_call = false;
}


void extmem_stop()
{

  policy_shutdown();

}


static void extmem_mmap_populate(void* addr, size_t length)
{
  // This is still buggy
  void* newptr;
  uint64_t offset;
  struct user_page *page;
  bool in_dram;
  uint64_t page_boundry;
  void* tmpaddr;
  uint64_t pagesize;

  assert(addr != 0);
  assert(length != 0);

  page_boundry = (uint64_t)addr;
  LOG("doing MAP_POPULATE for pages starting at 0x%lx until 0x%lx the length is %lu for %lu pages\n", 
  page_boundry, (page_boundry + length), length, length / PAGE_SIZE);
  
  // need to prefault all these pages
  for (page_boundry = (uint64_t)addr; page_boundry < (uint64_t)addr + length;) {
    
    handle_first_fault(page_boundry);
    page_boundry+=PAGE_SIZE;

  }
  LOG("MAP_POPULATE initialized %lu pages\n", (page_boundry - (uint64_t)addr) / PAGE_SIZE);
  return;

}

#define PAGE_ROUND_UP(x) (((x) + (PAGE_SIZE)-1) & (~((PAGE_SIZE)-1)))

void* extmem_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  void *p;
  int need_populate = false;
  internal_call = true;

  assert(is_init);
  assert(length != 0);

  
  if ((flags & MAP_SHARED) == MAP_SHARED) {
     flags &= ~MAP_SHARED;
     flags |= MAP_PRIVATE;
     LOG("extmem_mmap: changed flags to MAP_PRIVATE\n");
   }

  flags |= MAP_NORESERVE;
  flags |= MAP_ANONYMOUS;
  if ((flags & MAP_POPULATE) == MAP_POPULATE) {
    flags &= ~MAP_POPULATE;
    need_populate = 1;
    LOG("extmem_mmap: unset MAP_POPULATE\n");
  }
  //flags |= MAP_POPULATE;
  #ifdef POPULATE_ALL
    need_populate = 1;
  #endif
  LOG("extmem_mmap: set MAP_ANONYMOUS and MAP_NORESERVE and controlling MAP_POPULATE\n");
  

  // TODO: support hugepages via hugetlb
  if ((flags & MAP_HUGETLB) == MAP_HUGETLB) {
    flags &= ~MAP_HUGETLB;
    LOG("extmem_mmap: unset MAP_HUGETLB\n");
  }
  
  // reserve block of virtual memory
  length = PAGE_ROUND_UP(length);
  p = libc_mmap(addr, length, prot, flags, -1, offset);
  if (p == NULL || p == MAP_FAILED) {
    perror("mmap");
    assert(!"mmap failed");
  }
  assert(p != NULL && p != MAP_FAILED);

  main_mmap = p;
  // register with uffd
  struct uffdio_register uffdio_register;
  uffdio_register.range.start = (uint64_t)p;
  uffdio_register.range.len = length;
  uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP;
  uffdio_register.ioctls = 0;
  if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
    perror("ioctl uffdio_register");
    assert(0);
  }

   
  if (need_populate) {
    LOG("extmem_mmap: doing mmap_populate\n");
    //assert(!"map populate not implemented yet!");
    extmem_mmap_populate(p, length);
  }

  mem_mmaped = length;
  
  policy_ack_vma(p, length, 0);
  
  internal_call = false;
  LOG("extmem_mmap: finished, mapped at 0x%lx until 0x%lx\n", (uint64_t)p, (uint64_t)p + length);
  LOGFLUSH();
  return p;
}


int extmem_munmap(void* addr, size_t length)
{
  uint64_t page_boundry;
  struct user_page *page;
  int ret;

  internal_call = true;


  LOG("extmem_munmap(%p, %lu)\n", addr, length);
  
  //pthread_mutex_lock(handler_lock);
  pthread_mutex_lock(&allocator_lock);

  // TODO: fix races between munmap and policy
  // for each page in region specified...
  for (page_boundry = (uint64_t)addr; page_boundry < (uint64_t)addr + length;) {
    // find the page in extmem's tracking list
    page = find_page(page_boundry);
    if (page != NULL) {
      // remove page from hash and policy's list
      pthread_mutex_lock(&handler_lock);
      if(page->migrating == true){ // being handled
        pthread_mutex_unlock(&handler_lock);
      //LOG("munmap: waiting for migration for page %lx\n", page_boundry);
      
        while (page->migrating);  // release the lock, busy wait and retry
        pthread_mutex_lock(&handler_lock);
      
      }
      page->migrating = true;
      pthread_mutex_unlock(&handler_lock);

      remove_page(page);
      mmgr_remove(page);

      mem_allocated -= pt_to_pagesize(page->pt);
      mem_mmaped -= pt_to_pagesize(page->pt);
      pages_freed++;
      //pthread_mutex_lock(&handler_lock);
      //page->migrating = false;
      //pthread_mutex_unlock(&handler_lock);

      // move to next page
      page_boundry += pt_to_pagesize(page->pt);
    }
    else {
      // temporary way to handling
      page_boundry += BASEPAGE_SIZE;
    }
  }


  ret = libc_munmap(addr, length);
  if(ret){
    perror("libc munmap: ");
    assert(0);
  }
  pthread_mutex_unlock(&allocator_lock);
  
  internal_call = false;

  LOGFLUSH();
  return ret;
}


// Connect the paging system to the disk backend
// TODO: turn this from hardcoded fd to a object oriented storage manager
void extmem_disk_write(int storage_fd, void* src, uint64_t dest, size_t pagesize)
{
    write_page(storage_fd, dest, src, pagesize);
  
}

void extmem_disk_read(int storage_fd, uint64_t src, void *dest, size_t pagesize)
{
    read_page(storage_fd, (uint64_t)src, dest, pagesize);
  
}


void extmem_migrate_updisk(struct user_page *page, uint64_t dram_offset)
{
  //void *old_addr;
  // self-paging: determine our tid
  //int mytid = pthread_self();
  //void *local_buffer;
  void *new_addr;
  void *newptr;
  struct timeval migrate_start, migrate_end;
  struct timeval start, end;
  uint64_t disk_addr_offset, new_addr_offset;
  uint64_t pagesize;
  
  //internal_call = true;

  assert(!page->in_dram);

  // This method is bad because it has memory leak of 1 page
  // per every finishing thread. Assuming a program will not fire more than 1k threads, that is small enough to be ignored
  // for now.
  if(buffer_allocated == false){
    buffer_space = libc_mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE , -1, 0);
    if (buffer_space == MAP_FAILED) {
      perror("buffer space mmap");
      assert(0);
    }
    buffer_allocated = true;
    LOG("Buffer space mapped at %lx\n", (uint64_t)buffer_space);
    //fprintf(stderr, "Buffer space mapped at %lx\n", (uint64_t)buffer_space);
  
  }

  gettimeofday(&migrate_start, NULL);
  
  assert(page != NULL);

  pagesize = pt_to_pagesize(page->pt);

  disk_addr_offset = page->virtual_offset;
  new_addr_offset = dram_offset;

  assert((uint64_t)disk_addr_offset < disksize);
  
  assert((uint64_t)new_addr_offset < dramsize);
  

  // copy page from faulting location to temp location
  gettimeofday(&start, NULL);
#if defined(USWAP_SPDK)
  assert(!"spdk funcitons not implemented yet");
#elif defined (USWAP_IOURING)
  ioring_read_store(diskfd, disk_addr_offset, buffer_space, pagesize);
#else
  //extmem_disk_read(diskfd, disk_addr_offset, new_addr, pagesize);
  extmem_disk_read(diskfd, disk_addr_offset, (void*)buffer_space, pagesize);

#endif
  gettimeofday(&end, NULL);
  LOG_TIME("memcpy_to_dram: %f s\n", elapsed(&start, &end));
 

// Map the swapped-in page for use
  gettimeofday(&start, NULL);
  assert(libc_mmap != NULL);

  
  struct uffdio_copy copy = {
      .dst = (uint64_t)page->va
    , .src = (uint64_t)buffer_space
    , .len = pagesize
    , .mode = UFFDIO_COPY_MODE_DONTWAKE
  };

  // mapping page using UFFDIO
  if (ioctl(uffd, UFFDIO_COPY, &copy) == -1){
    perror("UFFDIO_COPY failed ");
    assert(0);
  }

  //pt_clear_bits(page);
  
  assert(page->va % PAGE_SIZE == 0);
  gettimeofday(&end, NULL);
  LOG_TIME("mmap_dram: %f s\n", elapsed(&start, &end));

  //page->migrations_up++;
  migrations_up++;

  page->virtual_offset = dram_offset;
  page->in_dram = true;

  bytes_migrated += pagesize;
  
  //LOG("extmem_migrate_up: new pte: %lx\n", extmem_va_to_pa(page->va));

  gettimeofday(&migrate_end, NULL);  
  LOG_TIME("extmem_migrate_up: %f s\n", elapsed(&migrate_start, &migrate_end));

  //internal_call = false;
}


void core_migrate_up_async_start(struct user_page *page)
{
  //void *old_addr;
  // self-paging: determine our tid
  //int mytid = pthread_self();
  //void *local_buffer;
  //void *new_addr;
  //void *newptr;
  struct timeval migrate_start, migrate_end;
  struct timeval start, end;
  uint64_t disk_addr_offset, new_addr_offset;
  uint64_t pagesize;
  
  //internal_call = true;

  assert(!page->in_dram);

  // This method is bad because it has memory leak of 1 page
  // per every finishing thread. Assuming a program will not fire more than 1k threads, that is small enough to be ignored
  // for now.
  if(buffer_allocated == false){
    buffer_space = libc_mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE , -1, 0);
    if (buffer_space == MAP_FAILED) {
      perror("buffer space mmap");
      assert(0);
    }
    buffer_allocated = true;
    LOG("Buffer space mapped at %lx\n", (uint64_t)buffer_space);
  }

  gettimeofday(&migrate_start, NULL);
  
  assert(page != NULL);

  pagesize = pt_to_pagesize(page->pt);

  disk_addr_offset = page->virtual_offset;
  //new_addr_offset = dram_offset;

  assert((uint64_t)disk_addr_offset < disksize);
  assert((uint64_t)disk_addr_offset + pagesize <= disksize);

  //assert((uint64_t)new_addr_offset < dramsize);
  //assert((uint64_t)new_addr_offset + pagesize <= dramsize);

  //LOG("uswap_paging_in page: %lu virtual address: %lx, from disk offset:%lu to dram offset:%lu using the buffer space at %lx\n", 
  //(((uint64_t)(page->va - main_mmap)) / PAGE_SIZE),(uint64_t)(page->va), (uint64_t)page->virtual_offset, (uint64_t)dram_offset, (uint64_t) buffer_space); 
  

  // copy page from faulting location to temp location
  gettimeofday(&start, NULL);
#if defined(USWAP_SPDK)
  assert(!"spdk funcitons not implemented yet");
#else
  ioring_start_read(diskfd, disk_addr_offset, buffer_space, pagesize);
#endif

  gettimeofday(&end, NULL);
  LOG_TIME("memcpy_to_dram: %f s\n", elapsed(&start, &end));
 

}

void core_migrate_up_async_finish(struct user_page *page, uint64_t dram_offset)
{
  //void *old_addr;
  // self-paging: determine our tid
  //int mytid = pthread_self();
  //void *local_buffer;
  void *new_addr;
  void *newptr;
  struct timeval migrate_start, migrate_end;
  struct timeval start, end;
  uint64_t disk_addr_offset, new_addr_offset;
  uint64_t pagesize;
  
  //internal_call = true;

  assert(!page->in_dram);

  assert(buffer_allocated == true);
  

  gettimeofday(&migrate_start, NULL);
  
  assert(page != NULL);

  pagesize = pt_to_pagesize(page->pt);

  disk_addr_offset = page->virtual_offset;
  new_addr_offset = dram_offset;

  assert((uint64_t)disk_addr_offset < disksize);
  assert((uint64_t)disk_addr_offset + pagesize <= disksize);

  assert((uint64_t)new_addr_offset < dramsize);
  assert((uint64_t)new_addr_offset + pagesize <= dramsize);

  //LOG("uswap_paging_in page: %lu virtual address: %lx, from disk offset:%lu to dram offset:%lu using the buffer space at %lx\n", 
  //(((uint64_t)(page->va - main_mmap)) / PAGE_SIZE),(uint64_t)(page->va), (uint64_t)page->virtual_offset, (uint64_t)dram_offset, (uint64_t) buffer_space); 
  

  // copy page from faulting location to temp location
  gettimeofday(&start, NULL);
#if defined(USWAP_SPDK)
  assert(!"spdk funcitons not implemented yet");
#else
  ioring_finish_read(diskfd, disk_addr_offset, buffer_space, pagesize);
#endif
  gettimeofday(&end, NULL);
  LOG_TIME("memcpy_to_dram: %f s\n", elapsed(&start, &end));
 

// Map the swapped-in page for use
  gettimeofday(&start, NULL);
  assert(libc_mmap != NULL);

  
  struct uffdio_copy copy = {
      .dst = (uint64_t)page->va
    , .src = (uint64_t)buffer_space
    , .len = pagesize
    , .mode = UFFDIO_COPY_MODE_DONTWAKE
  };

  // mapping page using UFFDIO
  if (ioctl(uffd, UFFDIO_COPY, &copy) == -1){
    perror("UFFDIO_COPY failed ");
    assert(0);
  }

  
  //if (newptr != (void*)page->va) {
  //  fprintf(stderr, "mapped address is not same as faulting address\n");
  //}
  assert(page->va % PAGE_SIZE == 0);
  gettimeofday(&end, NULL);
  LOG_TIME("mmap_dram: %f s\n", elapsed(&start, &end));

  //page->migrations_up++;
  migrations_up++;

  page->virtual_offset = dram_offset;
  page->in_dram = true;
  //page->pa = extmem_va_to_pa(page);

  bytes_migrated += pagesize;
  
  //LOG("extmem_migrate_up: new pte: %lx\n", extmem_va_to_pa(page->va));

  gettimeofday(&migrate_end, NULL);  
  LOG_TIME("extmem_migrate_up: %f s\n", elapsed(&migrate_start, &migrate_end));

  //internal_call = false;
}


void extmem_migrate_downdisk(struct user_page *page, uint64_t disk_offset, bool need_wp, bool writeback)
{
  void *old_addr;
  uint64_t new_addr;
  //void *newptr;
  struct timeval migrate_start, migrate_end;
  struct timeval start, end;
  uint64_t old_addr_offset, new_addr_offset;
  uint64_t pagesize;

  //LOG("uswap_paging_out page: %lu virtual address: %lx from dram address: %lu, to disk offset:%lu\n", 
  //  (((uint64_t)(page->va - main_mmap)) / PAGE_SIZE),(uint64_t)page->va, (uint64_t)page->virtual_offset, (uint64_t)disk_offset); 
  //internal_call = true;


  page->migrating = true;  // need to mutex lock?
  
  assert(page->in_dram);

  
  gettimeofday(&migrate_start, NULL);

  pagesize = pt_to_pagesize(page->pt);
  
  assert(page != NULL);
  old_addr_offset = page->virtual_offset;
  new_addr_offset = disk_offset;

  assert((uint64_t)old_addr_offset < dramsize);
  assert((uint64_t)old_addr_offset + pagesize <= dramsize);

  new_addr = new_addr_offset;
  assert((uint64_t)disk_offset < disksize);
  assert((uint64_t)disk_offset + pagesize <= disksize);

  // Policy may decide writeback is not needed
  if(writeback){
    // copy page from current location to disk, do we need an extra buffer for that?
    // TODO: can create a small swap cache and store it there
  
  #if defined(USWAP_SPDK)
    assert(!"spdk functions not implemented yet")
  #elif defined(USWAP_IOURING)
    //LOG("initiating io_uring write\n");
    ioring_write_store(diskfd, new_addr, page->va, pagesize);
  #else
    extmem_disk_write(diskfd, page->va, new_addr, pagesize);
  #endif
  }
  
  //gettimeofday(&start, NULL);
  
  // Unmap the physical mapping of the page
  // This is not portable but works as expected in Linux
  // Doing munmap would remove uffd mappings, doing mremap with some flags may work
  if (madvise(page->va, pagesize, MADV_DONTNEED) == -1){
        perror("madvise DONTNEED failed");
        assert(0);
  }

  
  // we except uffd registration of this virtual address
  // to remain so we can fault and swap in on next access

  //page->migrations_down++;
  migrations_down++;

  page->virtual_offset = disk_offset;
  page->in_dram = false;
  //page->swapped_out = true;
  //page->migrating = false;

  bytes_migrated += pagesize;

  pthread_mutex_lock(&handler_lock);
  assert(page->swapped_out == false);
  
  page->swapped_out = true;
  assert(page->migrating == true);
  //page->migrating = false; // let the policy decide when we are done
    
  pthread_mutex_unlock(&handler_lock);
    
  gettimeofday(&migrate_end, NULL);  
  LOG_TIME("extmem_migrate_down: %f s\n", elapsed(&migrate_start, &migrate_end));

  //internal_call = false;
}

void extmem_migrate_downdisk_vector(int nr_evicting, struct user_page **page, struct user_page **diskpage, bool *writeback)
{
  void *old_addr;
  uint64_t new_addr;
  //void *newptr;
  struct timeval migrate_start, migrate_end;
  struct timeval start, end;
  uint64_t old_addr_offset, new_addr_offset;
  //uint64_t pagesize;

  //LOG("uswap_paging_out page: %lu virtual address: %lx from dram address: %lu, to disk offset:%lu\n", 
  //  (((uint64_t)(page->va - main_mmap)) / PAGE_SIZE),(uint64_t)page->va, (uint64_t)page->virtual_offset, (uint64_t)disk_offset); 
  //internal_call = true;


  // for each page, get the offset and prepare the IO job if needed
  int nr_prepared = 0;
  int nr_submitted = 0;
  while(nr_prepared < nr_evicting){
    assert(page[nr_prepared]->in_dram);

    if(writeback[nr_prepared]){
      
      new_addr = diskpage[nr_prepared]->virtual_offset;

      #if defined(USWAP_SPDK)
        assert(!"spdk functions not implemented yet")
      #elif defined(USWAP_IOURING)
        //LOG("initiating io_uring write\n");
        ioring_prepare_write(diskfd, new_addr, page[nr_prepared]->va, PAGE_SIZE);
      #else
        extmem_disk_write(diskfd, page[nr_prepared]->va, new_addr, PAGE_SIZE);
      #endif      
      nr_submitted++;
    }

    nr_prepared++;

  }
  
  #ifdef USWAP_IOURING
  // submit together
  ioring_submit_all_writes();

  // receive the results
  nr_prepared = 0;
  while(nr_prepared < nr_submitted){
    ioring_finish_write();
    nr_prepared++;  
  }
  #endif

  
  nr_prepared = 0;
  while(nr_prepared < nr_evicting){  
    // Unmap the physical mapping of the page
    // This is not portable but works as expected in Linux
    // Doing munmap would remove uffd mappings, doing mremap with some flags may work
    if (madvise(page[nr_prepared]->va, PAGE_SIZE, MADV_DONTNEED) == -1){
          perror("madvise DONTNEED failed");
          assert(0);
    }
    nr_prepared++;
  }
  // we except uffd registration of this virtual address
  // to remain so we can fault and swap in on next access
  nr_prepared = 0;
  while(nr_prepared < nr_evicting){  
    migrations_down++;

    page[nr_prepared]->virtual_offset = diskpage[nr_prepared]->virtual_offset;
    page[nr_prepared]->in_dram = false;
    //page->swapped_out = true;
    //page->migrating = false;

    bytes_migrated += PAGE_SIZE;

    nr_prepared++;
  }
  //page->migrations_down++;
  nr_prepared = 0;
  pthread_mutex_lock(&handler_lock);  // lock should go in?
  while(nr_prepared < nr_evicting){
    assert(page[nr_prepared]->swapped_out == false);
  
    page[nr_prepared]->swapped_out = true;
    assert(page[nr_prepared]->migrating == true);
  //page->migrating = false; // let the policy decide when we are done
    nr_prepared++;
  }
  pthread_mutex_unlock(&handler_lock);
    
  //LOG("extmem_migrate_down: new pte: %lx\n", extmem_va_to_pa(page->va));

  gettimeofday(&migrate_end, NULL);  
  LOG_TIME("extmem_migrate_down: %f s\n", elapsed(&migrate_start, &migrate_end));

  //internal_call = false;
}


void extmem_wp_page(struct user_page *page, bool protect)
{
  uint64_t addr = page->va;
  struct uffdio_writeprotect wp;
  int ret;
  struct timeval start, end;
  uint64_t pagesize = pt_to_pagesize(page->pt);

  //internal_call = true;  // already set somewhere else

  //LOG("extmem_wp_page: wp addr %lx pte: %lx\n", addr, extmem_va_to_pa(addr));

  assert(addr != 0);
  assert(addr % PAGE_SIZE == 0);

  gettimeofday(&start, NULL);
  wp.range.start = addr;
  wp.range.len = pagesize;
  wp.mode = (protect ? UFFDIO_WRITEPROTECT_MODE_WP : 0);
  ret = ioctl(uffd, UFFDIO_WRITEPROTECT, &wp);

  if (ret < 0) {
    perror("uffdio writeprotect");
    assert(0);
  }
  gettimeofday(&end, NULL);

  LOG_TIME("uffdio_writeprotect: %f s\n", elapsed(&start, &end));

  //internal_call = false;
}


void handle_wp_fault(struct user_page *page)
{
  //struct user_page *page;

  //internal_call = true;

  //page = find_page(page_boundry);
  assert(page != NULL);

  migration_waits++;

  //LOG("extmem: handle_wp_fault: waiting for migration for page %lx\n", page_boundry);
  
  while (page->migrating);
  //internal_call = false;
}


void handle_missing_fault(struct user_page *page)
{
  // Page mising fault case - probably the first touch case
  // allocate in DRAM via LRU
  void* newptr;
  struct timeval missing_start, missing_end;
  struct timeval start, end;
  //struct user_page *page;
  struct user_page *newpage;
  uint64_t offset, swap_offset;
  uint64_t old_va;
  void* tmp_offset;
  void* tmpaddr;
  bool in_dram;
  uint64_t pagesize;
  uint64_t page_boundry;
  bool must_prefetch = false;
  int nr_prefetched = 0;
  internal_call = true;

  //assert(page_boundry != 0);

  //LOG("handle_missing_fault: %lx page: %lu\n", 
  //  (uint64_t)page_boundry, (((uint64_t)(page_boundry - main_mmap)) / PAGE_SIZE)); 
  page_boundry = page->va;

  // If this page already exists in the hash table
  // then it must be present but swapped out
  //page = find_page(page_boundry);
  if (page != NULL) {
    //assert(page->va == page_boundry);
    //assert(page->swapped_out == true);
    //assert(page->in_dram == false);
    //LOG("Encountered a swapped-out page va: %lx , disk_offset: %lx \n", (uint64_t)page->va,
    //  (uint64_t) page->virtual_offset);
    old_va = page->va;
    
    gettimeofday(&missing_start, NULL);

    // critical section, check no other thread is handling the same page
    pthread_mutex_lock(&handler_lock);
    if(page->migrating == true){ // being handled
      //handle_wp_fault(page_boundry); // bad code reuse
      migration_waits++;  // not so important to be atomic

      pthread_mutex_unlock(&handler_lock);
      //LOG("extmem: handle_missing_fault: waiting for migration for page %lx\n", page_boundry);
      
      while (page->migrating);  // release the lock, busy wait and retry
      return;
    }
    else{  // check if we need to handle
      if(page->swapped_out == false){  // already handled
        migration_waits++;
        pthread_mutex_unlock(&handler_lock); // release the lock and return
        while (page->migrating);  // release the lock, busy wait and retry
        return;
      }

    }
    // if we reach here means we will handle this
    page->migrating = true; 
    pthread_mutex_unlock(&handler_lock);


    if(((page_boundry == prev_fault_1 + PAGE_SIZE) && (page_boundry == prev_fault_2 + PAGE_SIZE * 2)) ||
     (must_prefetch == true && page_boundry <= prev_fault_1 + PAGE_SIZE * CORE_PREFETCH_RATE && page_boundry > prev_fault_1)){
      LOG("Chance for thread prefetching major fault\n");
      must_prefetch = true;
    }
    else{
      must_prefetch = false;
    }

    prev_fault_2 = prev_fault_1;
    prev_fault_1 = page_boundry;

    // prev_fault_global_2 = prev_fault_global_1;
    // prev_fault_global_1 = page_boundry;

    //gettimeofday(&start, NULL);
    // Critical path swapping in
    
    swap_offset = page->virtual_offset;
    //in_dram = newpage->in_dram;
    pagesize = pt_to_pagesize(page->pt);

    //page_swapin(page, newpage);
    //page_swapin_external(page);
    page_swapin_external_async(page);

    //safe_to_free = page_boundry;
    
    //pt_clear_bits(page);
    
    // VA of struct page should not change unless munmapped
    assert(page->va == old_va);
    //assert(page->va == (uint64_t)newptr);
    assert(page->va != 0);
    assert(page->va % PAGE_SIZE == 0);
    
    
    //LOG("extmem_missing_fault: va: %lx assigned to %s frame %lu  pte: %lx\n", page->va, (in_dram ? "DRAM" : "NVM"), page->virtual_offset / pagesize, extmem_va_to_pa(page->va));
    pthread_mutex_lock(&handler_lock);
    page->swapped_out = false;
    page->migrating = false;
    

    pthread_mutex_unlock(&handler_lock);
    
    //first_faults_handled++;
    major_faults_handled++;
    //pages_allocated++;
    gettimeofday(&missing_end, NULL);
    LOG_TIME("extmem_major_fault: %f s\n", elapsed(&missing_start, &missing_end));


  }
  else{
    // If we enter here means there is an unsolved race condition
    assert(!"This should have been handled by the new function (first_fault)\n");
    
  }

  #ifdef TRY_PREFETCH
    if(must_prefetch){
      //gettimeofday(&start, NULL);
      
      nr_prefetched = core_try_prefetch(page_boundry);
      LOG("core prefaulted %d pages first touch\n", nr_prefetched);
      
      //gettimeofday(&end, NULL);
      //LOG("prefetching took: %f s for %d pages\n", elapsed(&start, &end), nr_prefetched);

      //LOG("core prefetched %d pages on major fault\n", nr_prefetched);

    }
  #endif

  internal_call = false;
  
}



void handle_first_fault(uint64_t page_boundry)
{
  // first touch page, allocate new physical page
  void* newptr;
  struct timeval missing_start, missing_end;
  struct timeval start, end;
  struct user_page *page;
  struct user_page *newpage;
  uint64_t offset, swap_offset;
  uint64_t old_va;
  uint16_t nr_prefaulted;
  void* tmp_offset;
  void* tmpaddr;
  bool in_dram;
  bool must_prefault = false;
  uint64_t pagesize;

  internal_call = true;


  assert(page_boundry != 0);

  //LOG("handle_first_fault: %lx page: %lu\n", 
  //  (uint64_t)page_boundry, (((uint64_t)(page_boundry - main_mmap)) / PAGE_SIZE)); 
  
  // fetch a free page first
  // policy algorithm must provide this functionality
  newpage = pagefault(); 
  assert(newpage != NULL);
    

  pthread_mutex_lock(&allocator_lock);
  
  // Check the hash table again to see
  // if it has already been added by another thread
  page = find_page(page_boundry);  // this also synchronizes with add_page
  if (page != NULL) {
    // nothing to do, release the lock and return
    //assert(!"not a first touch!\n");
    // soembody must have done it
    pthread_mutex_unlock(&allocator_lock);
    // put the page back
    policy_put_freepage(newpage);
    while (page->migrating);  // release the lock, busy wait and retry
    return;
  }
  else{

    // Page is not hashed means it is first touch
    // Allocate a page
    gettimeofday(&missing_start, NULL);
    page = newpage;
    gettimeofday(&start, NULL);
    page->migrating = true;
    page->swapped_out = false;
    // track new page's virtual address
    assert(page->va == 0);
    page->va = (uint64_t)page_boundry;
    assert(page->va != 0);
    assert(page->va % PAGE_SIZE == 0);
    
    // as soon as got the new page, place in extmem's page tracking list
    add_page(page);

    // can release the allocator lock here
    pthread_mutex_unlock(&allocator_lock);
    
    if((page_boundry == prev_fault_global_1 + PAGE_SIZE) && (page_boundry == prev_fault_global_2 + PAGE_SIZE * 2)){
       LOG("Chance for global prefetching\n");
       must_prefault = true;
     }

    if((page_boundry == prev_fault_1 + PAGE_SIZE) && (page_boundry == prev_fault_2 + PAGE_SIZE * 2)){
      LOG("Chance for thread prefetching\n");
      must_prefault = true;
    }

    prev_fault_2 = prev_fault_1;
    prev_fault_1 = page_boundry;

    prev_fault_global_2 = prev_fault_global_1;
    prev_fault_global_1 = page_boundry;

    gettimeofday(&end, NULL);
    LOG_TIME("page_fault: %f s\n", elapsed(&start, &end));
    
    offset = page->virtual_offset;
    in_dram = page->in_dram;
    pagesize = pt_to_pagesize(page->pt);

    assert(in_dram);
 
  // need uffd zero page here instead of memset
  // struct uffdio_zeropage zeropage;

  // zeropage.range.start = page_boundry;
  // zeropage.range.len = pagesize;
  // zeropage.mode = UFFDIO_ZEROPAGE_MODE_DONTWAKE;

  // if (ioctl(uffd, UFFDIO_ZEROPAGE, &zeropage) == -1){
  //   perror("UFFDIO_ZEROPAGE failed ");
  //   assert(0);
  // }
  struct uffdio_copy copy = {
      .dst = (uint64_t)page->va
    , .src = (uint64_t)page_cache
    , .len = pagesize
    , .mode = UFFDIO_COPY_MODE_DONTWAKE
  };

  //gettimeofday(&start, NULL);

  // mapping page using UFFDIO
  if (ioctl(uffd, UFFDIO_COPY, &copy) == -1){
    perror("UFFDIO_COPY failed ");
    assert(0);
  }

  //gettimeofday(&end, NULL);
  //LOG("uffd copy: %f s\n", elapsed(&start, &end));

  
    page->migrating = false;
    //page->migrations_up = page->migrations_down = 0;
    page->swapped_out = false;
    //page->pa = extmem_va_to_pa(page);
    policy_add_page(page);
    mem_allocated += pagesize;

  #ifdef TRY_PREFAULT
    if(must_prefault){
      nr_prefaulted = core_try_prefault(page_boundry);
      LOG("core prefaulted %d pages first touch\n", nr_prefaulted);
    }
  #endif

    
    first_faults_handled++;
    pages_allocated++;
    gettimeofday(&missing_end, NULL);
    LOG_TIME("extmem_missing_fault: %f s\n", elapsed(&missing_start, &missing_end));

    // release the allocator lock
    //pthread_mutex_unlock(&allocator_lock);
    

  }
  internal_call = false;
  
}

#ifdef PURE_MICROBENCHMARK
void handle_first_fault_dummy(uint64_t page_boundry)
{


  struct uffdio_zeropage zeropage;

  zeropage.range.start = page_boundry;
  zeropage.range.len = PAGE_SIZE;
  //zero page.mode = UFFDIO_ZEROPAGE_MODE_DONTWAKE;
  zeropage.mode = 0;

  if (ioctl(uffd, UFFDIO_ZEROPAGE, &zeropage) == -1){
    perror("UFFDIO_ZEROPAGE failed ");
    assert(0);
  }
  

}
#endif

int core_try_prefault(uint64_t base_boundry)
{
  // Try to prefault some subsequent pages
  void* newptr;
  uint64_t page_boundry;
  int nr_prefault = 0;
  struct timeval missing_start, missing_end;
  struct timeval start, end;
  struct user_page *page;
  struct user_page *newpage;
  uint64_t offset, swap_offset;
  uint64_t old_va;
  uint16_t nr_prefaulted;
  void* tmp_offset;
  void* tmpaddr;
  bool in_dram;
  uint64_t pagesize;
  int ret = 0;
  //internal_call = true;


  assert(base_boundry != 0);


  // fetch a free page first
  page_boundry = base_boundry;

  while(nr_prefault < CORE_PREFAULT_RATE && ret == 0){
    page_boundry += PAGE_SIZE;
    newpage = pagefault(); 
    assert(newpage != NULL);
  
    pthread_mutex_lock(&allocator_lock);
    
    // Check the hash table again to see
    // if it has already been added by another thread
    page = find_page(page_boundry);  // this also synchronizes with add_page
    if (page != NULL) {
      // nothing to do, release the lock and return
      //assert(!"not a first touch!\n");
      // soembody must have done it
      pthread_mutex_unlock(&allocator_lock);
      // put the page back
      //LOG("leaving prefault because page already existed\n");
      policy_put_freepage(newpage);
      //while (page->migrating);  // release the lock, busy wait and retry
      ret = -1;
    }
    else{

      // first try to see if this area is controlled at all
      struct uffdio_copy copy = {
        .dst = (uint64_t)page_boundry
      , .src = (uint64_t)page_cache
      , .len = PAGE_SIZE
      , .mode = UFFDIO_COPY_MODE_DONTWAKE
    };

    // mapping page using UFFDIO
    ret = ioctl(uffd, UFFDIO_COPY, &copy);
    if (ret == -1){
      //perror("ioctl failed");
      //LOG("leaving prefault because ioctl failed\n");
      
      // have to exit prefaulting now
      pthread_mutex_unlock(&allocator_lock);
      policy_put_freepage(newpage);
      break;
    }
      // Page is not hashed means it is first touch
      // Allocate in DRAM
      //gettimeofday(&missing_start, NULL);
      page = newpage;
      //gettimeofday(&start, NULL);
      page->migrating = true;
      page->swapped_out = false;
      // track new page's virtual address
      assert(page->va == 0);
      page->va = (uint64_t)page_boundry;
      assert(page->va != 0);
      assert(page->va % PAGE_SIZE == 0);
      
      // as soon as got the new page, place in extmem's page tracking list
      add_page(page);

      page->migrating = false;
      //page->migrations_up = page->migrations_down = 0;
      page->swapped_out = false;
      
      // can release the allocator lock here
      pthread_mutex_unlock(&allocator_lock);
      
    // need uffd zero page here instead of memset
    // struct uffdio_zeropage zeropage;

    // zeropage.range.start = page_boundry;
    // zeropage.range.len = pagesize;
    // zeropage.mode = UFFDIO_ZEROPAGE_MODE_DONTWAKE;

    // if (ioctl(uffd, UFFDIO_ZEROPAGE, &zeropage) == -1){
    //   perror("UFFDIO_ZEROPAGE failed ");
    //   assert(0);
    // }
      //page->pa = extmem_va_to_pa(page);
      policy_add_page(page);
      mem_allocated += pagesize;

      
      pages_allocated++;
      //gettimeofday(&missing_end, NULL);
      //LOG_TIME("extmem_missing_fault: %f s\n", elapsed(&missing_start, &missing_end));

      // release the allocator lock
      //pthread_mutex_unlock(&allocator_lock);
      //was a successful prefault
      nr_prefault++;

    }
  }

  return nr_prefault;  // return number of successful prefaults
  //internal_call = false;
  
}

int core_try_prefetch(uint64_t base_boundry)
{
  // Try to prefault some subsequent pages
  void* newptr;
  uint64_t page_boundry;
  int nr_prefetch = 0;
  struct user_page* prefetch_list[CORE_PREFETCH_RATE];
  struct user_page* freepage_list[CORE_PREFETCH_RATE];
  
  struct timeval missing_start, missing_end;
  struct timeval start, end;
  struct user_page *page;
  struct user_page *freepage;
  uint64_t offset, disk_addr_offset;
  uint64_t old_va;
  void* tmp_offset;
  void* tmpaddr;
  bool in_dram;
  uint64_t pagesize;
  int ret = 0;
  //internal_call = true;


  assert(base_boundry != 0);

  page_boundry = base_boundry;


  // first find pages elligible to be prefetched and detach them from their lists
  while(nr_prefetch < CORE_PREFETCH_RATE && ret == 0){
    page_boundry += PAGE_SIZE;
  
    // If this page already exists in the hash table
  // then it must be present but swapped out
  page = find_page(page_boundry);
  if (page != NULL) {
    assert(page->va == page_boundry);

      old_va = page->va;
    //perror("swapped out");
    //assert(0);
    gettimeofday(&missing_start, NULL);

    // critical section, check no other thread is handling the same page
    pthread_mutex_lock(&handler_lock);
    if(page->migrating == true){ // being handled
      migration_waits++;  // not so important to be atomic

      pthread_mutex_unlock(&handler_lock);

      ret = -1;
      continue;
    }
    else{  // check if we need to handle
      if(page->swapped_out == false){  // already handled
        migration_waits++;
        pthread_mutex_unlock(&handler_lock); // release the lock and return
        //fprintf(stderr, "already handled, don't prefetch more, nr_prefetch = %d\n", nr_prefetch);
    
        ret = -1;
        continue;
      }
    }
    if(page->va != old_va){ // something has changed, quit
       
        pthread_mutex_unlock(&handler_lock); // release the lock and return
        //fprintf(stderr, "Page VA had changed before prefetching\n");
    
        ret = -1;
        continue;


    }
    // if we reach here means we will handle this
    page->migrating = true; 

    // release the lock as soon as possible
    pthread_mutex_unlock(&handler_lock);

    // detach the page from its list
    assert(page->va != 0);
    policy_detach_page(page);
    prefetch_list[nr_prefetch] = page;
    nr_prefetch++;

  }
  else{
    // reached the boundary, don't prefetch more
    //LOG("reached the boundary, don't prefetch more, nr_prefetch = %d\n", nr_prefetch);
    ret = -1;
    break;
  }
    
  }

  // at this point we have the list of pages to prefetch
  // submit them to IO and go to find free pages

  // we need a prefetch buffer and we allocate it on-demand
  // this is bad because every dead thread will have a leaked prefetch buffer
  // TODO: initialize a proper prefetch buffer without memory leak
  if(prefetch_buffer_allocated == false){
    prefetch_buffer = libc_mmap(NULL, PAGE_SIZE * CORE_PREFETCH_RATE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE , -1, 0);
    if (prefetch_buffer == MAP_FAILED) {
      perror("prefetch buffer space mmap");
      assert(0);
    }
    prefetch_buffer_allocated = true;
    LOG("prefetch buffer space mapped at %lx\n", (uint64_t)buffer_space);
    
  }

  int submitted_jobs = 0;
  uint64_t buffer_pointer = (uint64_t)prefetch_buffer;

  while(submitted_jobs < nr_prefetch){
    page = prefetch_list[submitted_jobs];
    disk_addr_offset = page->virtual_offset;
  
    #if defined(USWAP_SPDK)
      assert(!"spdk funcitons not implemented yet");
    #else
      ioring_prepare_read(diskfd, disk_addr_offset, (void*)buffer_pointer, PAGE_SIZE);
    #endif

    LOG("job submitted: %d\n", submitted_jobs);
  
    submitted_jobs++;
    buffer_pointer += PAGE_SIZE;
  }

  ioring_submit_all_reads();
    
  assert(submitted_jobs == nr_prefetch);

  // collect free pages from policy
  int nr_free = 0;

  while(nr_free < nr_prefetch){

    freepage_list[nr_free] = pagefault();

    assert(freepage_list[nr_free] != NULL);

    nr_free++;
  } 

  // finish IO jobs 
  submitted_jobs = 0;
  buffer_pointer = (uint64_t)prefetch_buffer;

  while(submitted_jobs < nr_prefetch){
    page = prefetch_list[submitted_jobs];
    disk_addr_offset = page->virtual_offset;
  
    #if defined(USWAP_SPDK)
      assert(!"spdk funcitons not implemented yet");
    #else
      ioring_finish_read(diskfd, disk_addr_offset, (void*)buffer_pointer, PAGE_SIZE);
    #endif

    submitted_jobs++;
    buffer_pointer += PAGE_SIZE;
  }

  int finished_jobs = 0;
  buffer_pointer = (uint64_t)prefetch_buffer;

  while(finished_jobs < nr_prefetch){
    page = prefetch_list[finished_jobs];
    freepage = freepage_list[finished_jobs];



    struct uffdio_copy copy = {
        .dst = (uint64_t)page->va
      , .src = (uint64_t)buffer_pointer
      , .len = PAGE_SIZE
      , .mode = UFFDIO_COPY_MODE_DONTWAKE
    };

    // mapping page using UFFDIO
    if (ioctl(uffd, UFFDIO_COPY, &copy) == -1){
      perror("UFFDIO_COPY failed ");
      assert(0);
    }

    policy_ack_swapped_in(page, freepage);
    page->in_dram = true;
    // anything else?
    pthread_mutex_lock(&handler_lock);
    page->swapped_out = false;
    page->migrating = false;
    

    pthread_mutex_unlock(&handler_lock);

    finished_jobs++;
    buffer_pointer += PAGE_SIZE;
  }
  
  assert(finished_jobs == nr_prefetch);
  
  return finished_jobs;  // return number of successful prefaults
  //internal_call = false;
  
}



#ifdef USWAP_UFFD
// This is the fault handler thread which does the polling
void *handle_fault()
{
  static struct uffd_msg msg[MAX_UFFD_MSGS];
  ssize_t nread;
  uint64_t fault_addr;
  uint64_t fault_flags;
  uint64_t page_boundry;
  struct timeval start, end;
  
  struct uffdio_range range;
  int ret;
  int nmsgs;
  int i;

  cpu_set_t cpuset;
  pthread_t thread;

  thread = pthread_self();
  CPU_ZERO(&cpuset);
  CPU_SET(FAULT_THREAD_CPU, &cpuset);
  int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (s != 0) {
    perror("pthread_setaffinity_np");
    assert(0);
  }
  struct pollfd pollfd;
    int pollres;
    pollfd.fd = uffd;
    pollfd.events = POLLIN;

  for (;;) {
    
     //gettimeofday(&start, NULL);
      
    pollres = poll(&pollfd, 1, -1);
    
    
    
    switch (pollres) {
    case -1:
      perror("poll");
      assert(0);
    case 0:
      fprintf(stderr, "poll read 0\n");
      continue;
    case 1:
      break;
    default:
      fprintf(stderr, "unexpected poll result\n");
      assert(0);
    }

    if (pollfd.revents & POLLERR) {
      fprintf(stderr, "pollerr\n");
      assert(0);
    }

    if (!pollfd.revents & POLLIN) {
      continue;
    }
        
    nread = read(uffd, &msg[0], MAX_UFFD_MSGS * sizeof(struct uffd_msg));
    if (nread == 0) {
      fprintf(stderr, "EOF on userfaultfd\n");
      assert(0);
    }

    
    if (nread < 0) {
      if (errno == EAGAIN) {
        continue;
      }
      perror("read");
      assert(0);
    }

    if ((nread % sizeof(struct uffd_msg)) != 0) {
      fprintf(stderr, "invalid msg size: [%ld]\n", nread);
      assert(0);
    }

    nmsgs = nread / sizeof(struct uffd_msg);
    
    for (i = 0; i < nmsgs; i++) {
      
      if (msg[i].event & UFFD_EVENT_PAGEFAULT) {
        fault_addr = (uint64_t)msg[i].arg.pagefault.address;
        fault_flags = msg[i].arg.pagefault.flags;
        
        // allign faulting address to page boundry
        page_boundry = fault_addr & ~(PAGE_SIZE - 1);
        
    
#ifdef PURE_MICROBENCHMARK
        handle_first_fault_dummy(page_boundry);
        continue;
#endif
        // gettimeofday(&end, NULL);
      //fprintf(stderr, "fault handled in: %f s\n", elapsed(&start, &end));
  
       
        if (fault_flags & UFFD_PAGEFAULT_FLAG_WP) {
         handle_wp_fault(page_boundry);
        }
        else {
         handle_missing_fault(page_boundry);
        }
           
        // wake the faulting thread
        range.start = (uint64_t)page_boundry;
        range.len = PAGE_SIZE;

        ret = ioctl(uffd, UFFDIO_WAKE, &range);

        if (ret < 0) {
          perror("uffdio wake");
          assert(0);
        }
       
        
      }
      else if (msg[i].event & UFFD_EVENT_UNMAP){
        fprintf(stderr, "Received an unmap event\n");
        assert(0);
      }
      else if (msg[i].event & UFFD_EVENT_REMOVE) {
        fprintf(stderr, "received a remove event\n");
        assert(0);
      }
      else {
        fprintf(stderr, "received a non page fault event\n");
        assert(0);
      }
       
  
    }

    
  }
}
#endif

void extmem_print_stats()
{

  LOG_STATS("mem_allocated: [%lu]\tpages_allocated: [%lu]\tfirst_faults_handled: [%lu]\tmajor_faults_handled: [%lu]\tbytes_migrated: [%lu]\tswap_ins: [%lu]\tswap_outs: [%lu]\tmigration_waits: [%lu]\n", 
                mem_allocated, 
                pages_allocated, 
                first_faults_handled, 
                major_faults_handled,
                bytes_migrated,
                migrations_up, 
                migrations_down,
                migration_waits);
  mmgr_stats(); 
  
}


void extmem_clear_stats()
{
  pages_allocated = 0;
  pages_freed = 0;
  first_faults_handled = 0;
  major_faults_handled = 0;
  migrations_up = 0;
  migrations_down = 0;
}

struct user_page* get_user_page(uint64_t va)
{
  return find_page(va);
}

