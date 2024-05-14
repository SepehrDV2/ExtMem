#ifndef FIFO_H
#define FIFO_H

#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

#include "core.h"

struct fifo_list {
  struct user_page *first, *last;
  pthread_mutex_t list_lock;
  size_t numentries;
};


void enqueue_fifo(struct fifo_list *list, struct user_page *page);
struct user_page* dequeue_fifo(struct fifo_list *list);
void page_list_remove_page(struct fifo_list *list, struct user_page *page);
void next_page(struct fifo_list *list, struct user_page *page, struct user_page **res);

int page_list_tryremove_page(struct fifo_list *list, struct user_page *page);
int dequeue_fifo_vector( struct fifo_list *queue, struct user_page **vector, int nr_fetch);

#endif
