#ifndef OBSERVABILITY_H
#define OBSERVABILITY_H

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>


extern long uffd;

void pt_clear_bits(struct user_page *page);
void pt_clear_accessed_flag(struct user_page *page);
void pt_clear_dirty_flag(struct user_page *page);
uint64_t pt_get_bits(struct user_page *page);
//void kernel_tlb_shootdown(uint64_t va);


#define ADDRESS_MASK  ((uint64_t)0x00000ffffffff000UL)
#define FLAGS_MASK  ((uint64_t)0x0000000000000fffUL)

#define PT_ACCESSED_FLAG ((uint64_t)0x0000000000000020UL)
#define PT_DIRTY_FLAG  ((uint64_t)0x0000000000000040UL)
#define PT_HUGEPAGE_FLAG ((uint64_t)0x0000000000000080UL)


#endif
