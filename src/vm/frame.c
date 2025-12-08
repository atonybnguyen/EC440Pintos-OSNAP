#include "vm/frame.h"
#include <stdio.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "userprog/syscall.h"
#include "threads/interrupt.h"

static struct list frame_table;     /* List of all frames */
static struct lock frame_lock;      /* Lock for frame table */
static struct list_elem *clock_hand;/* Clock hand for eviction algorithm */

static struct frame_entry *find_frame(void *kpage);
static void *evict_frame(void);

/* Initialize the frame table */
void 
frame_init(void)
{
  list_init(&frame_table);
  lock_init(&frame_lock);
  clock_hand = NULL;
}

/* Allocate a frame */
void *
frame_alloc(enum palloc_flags flags, void *upage)
{
  ASSERT(flags & PAL_USER);
  
  void *kpage = palloc_get_page(flags);
  
  if (kpage == NULL)
    {
      kpage = evict_frame();
      if (kpage == NULL)
        PANIC("Out of memory - cannot evict frame");
      
      if (flags & PAL_ZERO)
        memset(kpage, 0, PGSIZE);
    }
  
  struct frame_entry *entry = malloc(sizeof(struct frame_entry));
  if (entry == NULL)
    {
      palloc_free_page(kpage);
      return NULL;
    }
  
  entry->kpage = kpage;
  entry->upage = upage;
  entry->owner = thread_current();
  entry->pinned = false;
  
  enum intr_level old_level = intr_disable();
  list_push_back(&frame_table, &entry->elem);
  intr_set_level(old_level);
  
  return kpage;
}

void 
frame_free(void *kpage)
{
  enum intr_level old_level = intr_disable();
  
  struct frame_entry *entry = find_frame(kpage);
  if (entry != NULL)
    {
      if (clock_hand == &entry->elem)
        {
          clock_hand = list_next(clock_hand);
          if (clock_hand == list_end(&frame_table))
            clock_hand = list_begin(&frame_table);
        }
      
      list_remove(&entry->elem);
      free(entry);
    }
  
  intr_set_level(old_level);
  
  palloc_free_page(kpage);
}

/* Pin a frame (prevent eviction) */
void 
frame_pin(void *kpage)
{
  lock_acquire(&frame_lock);
  
  struct frame_entry *entry = find_frame(kpage);
  if (entry != NULL)
    entry->pinned = true;
  
  lock_release(&frame_lock);
}

/* Unpin a frame (allow eviction) */
void 
frame_unpin(void *kpage)
{
  lock_acquire(&frame_lock);
  
  struct frame_entry *entry = find_frame(kpage);
  if (entry != NULL)
    entry->pinned = false;
  
  lock_release(&frame_lock);
}

/* Find frame entry by kernel page address */
static struct frame_entry *
find_frame(void *kpage)
{
  struct list_elem *e;
  
  for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
      struct frame_entry *entry = list_entry(e, struct frame_entry, elem);
      if (entry->kpage == kpage)
        return entry;
    }
  
  return NULL;
}

/* Evict a frame using clock algorithm */
static void *
evict_frame(void)
{
  enum intr_level old_level = intr_disable();
  
  if (list_empty(&frame_table))
    {
      intr_set_level(old_level);
      return NULL;
    }
  
  if (clock_hand == NULL || clock_hand == list_end(&frame_table))
    clock_hand = list_begin(&frame_table);
  
  struct frame_entry *victim = NULL;
  size_t iterations = 0;
  size_t max_iterations = list_size(&frame_table) * 2;
  
  while (iterations < max_iterations)
    {
      struct frame_entry *entry = list_entry(clock_hand, struct frame_entry, elem);
      
      if (!entry->pinned)
        {
          uint32_t *pd = entry->owner->pagedir;
          
          if (pagedir_is_accessed(pd, entry->upage))
            pagedir_set_accessed(pd, entry->upage, false);
          else
            {
              victim = entry;
              break;
            }
        }
      
      clock_hand = list_next(clock_hand);
      if (clock_hand == list_end(&frame_table))
        clock_hand = list_begin(&frame_table);
      
      iterations++;
    }
  
  if (victim == NULL)
    {
      intr_set_level(old_level);
      return NULL;
    }
  
  void *kpage = victim->kpage;
  void *upage = victim->upage;
  struct thread *owner = victim->owner;
  uint32_t *pd = owner->pagedir;
  bool dirty = pagedir_is_dirty(pd, upage);
  
  /* Clear page table entry first */
  pagedir_clear_page(pd, upage);
  
  /* Move clock hand */
  clock_hand = list_next(&victim->elem);
  if (clock_hand == list_end(&frame_table))
    clock_hand = list_begin(&frame_table);
  
  /* Remove from frame table */
  list_remove(&victim->elem);
  free(victim);
  
  /* Re-enable interrupts */
  intr_set_level(old_level);
  
  /* Update SPT */
  struct spt_entry *spt_entry = spt_get_entry(&owner->spt, upage);
  
  if (spt_entry != NULL)
    {
      lock_acquire(&owner->spt.lock);
      
      /* Mark as not loaded FIRST to prevent races */
      spt_entry->loaded = false;
      spt_entry->kpage = NULL;
      
      bool is_mmap = (spt_entry->type == PAGE_MMAP);
      bool is_writable = spt_entry->writable;
      struct file *file = spt_entry->file;
      off_t file_offset = spt_entry->file_offset;
      uint32_t read_bytes = spt_entry->read_bytes;
      
      lock_release(&owner->spt.lock);
      
      /* Now do I/O operations */
      if (is_mmap && dirty)
        {
          lock_acquire(&file_lock);
          file_seek(file, file_offset);
          file_write(file, kpage, read_bytes);
          lock_release(&file_lock);
        }
      else if (!is_mmap && (dirty || is_writable))
        {
          size_t swap_slot = swap_out(kpage);
          
          lock_acquire(&owner->spt.lock);
          spt_entry->type = PAGE_SWAP;
          spt_entry->swap_slot = swap_slot;
          lock_release(&owner->spt.lock);
        }
    }
  
  return kpage;
}
