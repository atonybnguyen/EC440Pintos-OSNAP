#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/palloc.h"
#include "threads/synch.h"

/* Frame table entry */
struct frame_entry 
{
  void *kpage;              /* Kernel virtual address */
  void *upage;              /* User virtual address */
  struct thread *owner;     /* Owning thread */
  bool pinned;              /* Whether frame is pinned (cannot be evicted) */
  struct list_elem elem;    /* List element for frame table */
};

/* Initialize the frame table */
void frame_init(void);

/* Allocate a frame */
void *frame_alloc(enum palloc_flags flags, void *upage);

/* Free a frame */
void frame_free(void *kpage);

/* Pin a frame (prevent eviction) */
void frame_pin(void *kpage);

/* Unpin a frame (allow eviction) */
void frame_unpin(void *kpage);

#endif /* vm/frame.h */
