#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <syscall.h>
#include <errno.h>
#define __USE_GNU
#include <dlfcn.h>
#include <pthread.h>
#include <sys/mman.h>
#include <assert.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <linux/userfaultfd.h>

#include "core.h"
#include "observability.h"


void kernel_tlb_shootdown(uint64_t va)
{
  uint64_t page_boundry = va & ~(PAGE_SIZE - 1);
  struct uffdio_range range;
  int ret;
  
  range.start = page_boundry;
  range.len = PAGE_SIZE;

  ret = ioctl(uffd, UFFDIO_TLBFLUSH, &range);
  if (ret < 0) {
    perror("uffdio tlbflush");
    assert(0);
  }
}

void pt_clear_bits(struct user_page *page)
{
  uint64_t ret;
  struct uffdio_page_flags page_flags;

  page_flags.va = page->va;
  assert(page_flags.va % PAGE_SIZE == 0);
  page_flags.flag1 = PT_ACCESSED_FLAG;
  page_flags.flag2 = PT_DIRTY_FLAG;

  if (ioctl(uffd, UFFDIO_CLEAR_FLAG, &page_flags) < 0) {
    fprintf(stderr, "userfaultfd_clear_flag returned < 0\n");
    assert(0);
  }

  ret = page_flags.res1;
  if (ret == 0) {
    LOG("extmem_clear_accessed_bit: accessed bit not cleared\n");
  }
}

void pt_clear_accessed_flag(struct user_page *page)
{
  uint64_t ret;
  struct uffdio_page_flags page_flags;

  page_flags.va = page->va;
  assert(page_flags.va % PAGE_SIZE == 0);
  page_flags.flag1 = PT_ACCESSED_FLAG;
  page_flags.flag2 = 0;

  if (ioctl(uffd, UFFDIO_CLEAR_FLAG, &page_flags) < 0) {
    fprintf(stderr, "userfaultfd_clear_flag returned < 0\n");
    assert(0);
  }

  ret = page_flags.res1;
  if (ret == 0) {
    LOG("extmem_clear_accessed_bit: accessed bit not cleared\n");
  }
}

void pt_clear_dirty_flag(struct user_page *page)
{
  uint64_t ret;
  struct uffdio_page_flags page_flags;

  page_flags.va = page->va;
  assert(page_flags.va % PAGE_SIZE == 0);
  page_flags.flag1 = PT_DIRTY_FLAG;
  page_flags.flag2 = 0;

  if (ioctl(uffd, UFFDIO_CLEAR_FLAG, &page_flags) < 0) {
    fprintf(stderr, "userfaultfd_clear_flag returned < 0\n");
    assert(0);
  }

  ret = page_flags.res1;
  if (ret == 0) {
    LOG("extmem_clear_accessed_bit: accessed bit not cleared\n");
  }
}

uint64_t pt_get_bits(struct user_page *page)
{
  uint64_t ret;
  struct uffdio_page_flags page_flags;

  page_flags.va = page->va;
  assert(page_flags.va % PAGE_SIZE == 0);
  page_flags.flag1 = PT_ACCESSED_FLAG;
  page_flags.flag2 = PT_DIRTY_FLAG;

  if (ret = ioctl(uffd, UFFDIO_GET_FLAG, &page_flags) < 0) {
    fprintf(stderr, "userfaultfd_get_flag returned < 0\n");
    assert(0);
  }
  ret = page_flags.res1 | page_flags.res2;
  
  return ret;
}
