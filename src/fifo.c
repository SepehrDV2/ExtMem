#include <pthread.h>
#include <stdlib.h>

#include "fifo.h"
#include "core.h"

void enqueue_fifo(struct fifo_list *queue, struct user_page *entry)
{
  pthread_mutex_lock(&(queue->list_lock));
  assert(entry->prev == NULL);
  entry->next = queue->first;
  if(queue->first != NULL) {
    assert(queue->first->prev == NULL);
    queue->first->prev = entry;
  } else {
    assert(queue->last == NULL);
    assert(queue->numentries == 0);
    queue->last = entry;
  }

  queue->first = entry;
  entry->list = queue;
  queue->numentries++;
  pthread_mutex_unlock(&(queue->list_lock));
}

struct user_page *dequeue_fifo(struct fifo_list *queue)
{
  pthread_mutex_lock(&(queue->list_lock));
  struct user_page *ret = queue->last;

  if(ret == NULL) {
    //assert(queue->numentries == 0);
    pthread_mutex_unlock(&(queue->list_lock));
    return ret;
  }

  queue->last = ret->prev;
  if(queue->last != NULL) {
    queue->last->next = NULL;
  } else {
    queue->first = NULL;
  }

  ret->prev = ret->next = NULL;
  ret->list = NULL;
  assert(queue->numentries > 0);
  queue->numentries--;
  pthread_mutex_unlock(&(queue->list_lock));

  return ret;
}

void page_list_remove_page(struct fifo_list *list, struct user_page *page)
{
  pthread_mutex_lock(&(list->list_lock));
  if (list->first == NULL) {
    assert(list->last == NULL);
    assert(list->numentries == 0);
    pthread_mutex_unlock(&(list->list_lock));
    LOG("page_list_remove_page: list was empty!\n");
    return;
  }

  if (list->first == page) {
    list->first = page->next;
  }

  if (list->last == page) {
    list->last = page->prev;
  }

  if (page->next != NULL) {
    page->next->prev = page->prev;
  }

  if (page->prev != NULL) {
    page->prev->next = page->next;
  }

  assert(list->numentries > 0);
  list->numentries--;
  page->next = NULL;
  page->prev = NULL;
  page->list = NULL;
  pthread_mutex_unlock(&(list->list_lock));
}

int page_list_tryremove_page(struct fifo_list *list, struct user_page *page)
{
  // null input possible
  if(list == NULL){
    return -1;
  }
  pthread_mutex_lock(&(list->list_lock));
  // not our list
  if(page->list != list){
    pthread_mutex_unlock(&(list->list_lock));
    return -1;
  }
  if (list->first == NULL) {
    assert(list->last == NULL);
    assert(list->numentries == 0);
    pthread_mutex_unlock(&(list->list_lock));
    LOG("page_list_remove_page: list was empty!\n");
    return -1;
  }

  if (list->first == page) {
    list->first = page->next;
  }

  if (list->last == page) {
    list->last = page->prev;
  }

  if (page->next != NULL) {
    page->next->prev = page->prev;
  }

  if (page->prev != NULL) {
    page->prev->next = page->next;
  }

  assert(list->numentries > 0);
  list->numentries--;
  page->next = NULL;
  page->prev = NULL;
  page->list = NULL;
  pthread_mutex_unlock(&(list->list_lock));
  return 0;
}

void next_page(struct fifo_list *list, struct user_page *page, struct user_page **next_page)
{
    pthread_mutex_lock(&(list->list_lock));
    if (page == NULL) {
        *next_page = list->last;
    }
    else {
        *next_page = page->prev;
        assert(page->list == list);
    }
    pthread_mutex_unlock(&(list->list_lock));
}

// try fetching n pages or until it's empty
int dequeue_fifo_vector( struct fifo_list *queue, struct user_page **vector, int nr_fetch)
{
  //int ret = 0;
  int nr_taken = 0;
  struct user_page *page;
  pthread_mutex_lock(&(queue->list_lock));
  
  while(nr_taken < nr_fetch){
    
    page = queue->last;
  
    if(page == NULL) { // nothing to take
      //assert(queue->numentries == 0);
      //pthread_mutex_unlock(&(queue->list_lock));
      break;
    }

    queue->last = page->prev;
    if(queue->last != NULL) {
      queue->last->next = NULL;
    } else {
      queue->first = NULL;
    }

    page->prev = page->next = NULL;
    page->list = NULL;
    assert(queue->numentries > 0);
    queue->numentries--;
    vector[nr_taken] = page;
    nr_taken++;
  }

  pthread_mutex_unlock(&(queue->list_lock));

  return nr_taken;
}
