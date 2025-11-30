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
  
  /* If allocation fails, try evicting a frame */
  if (kpage == NULL)
    {
      kpage = evict_frame();
      if (kpage == NULL)
        PANIC("Out of memory - cannot evict frame");
      
      /* Zero the frame if requested */
      if (flags & PAL_ZERO)
        memset(kpage, 0, PGSIZE);
    }
  
  /* Create frame entry */
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
  
  lock_acquire(&frame_lock);
  list_push_back(&frame_table, &entry->elem);
  lock_release(&frame_lock);
  
  return kpage;
}

/* Free a frame */
void 
frame_free(void *kpage)
{
  lock_acquire(&frame_lock);
  
  struct frame_entry *entry = find_frame(kpage);
  if (entry != NULL)
    {
      /* If clock hand points to this frame, move it */
      if (clock_hand == &entry->elem)
        {
          clock_hand = list_next(clock_hand);
          if (clock_hand == list_end(&frame_table))
            clock_hand = list_begin(&frame_table);
        }
      
      list_remove(&entry->elem);
      free(entry);
    }
  
  lock_release(&frame_lock);
  
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
  lock_acquire(&frame_lock);
  
  if (list_empty(&frame_table))
    {
      lock_release(&frame_lock);
      return NULL;
    }
  
  /* Initialize clock hand if needed */
  if (clock_hand == NULL || clock_hand == list_end(&frame_table))
    clock_hand = list_begin(&frame_table);
  
  struct frame_entry *victim = NULL;
  size_t iterations = 0;
  size_t max_iterations = list_size(&frame_table) * 2;
  
  /* Clock algorithm: look for unpinned, unaccessed page */
  while (iterations < max_iterations)
    {
      struct frame_entry *entry = list_entry(clock_hand, struct frame_entry, elem);
      
      /* Skip pinned frames */
      if (!entry->pinned)
        {
          uint32_t *pd = entry->owner->pagedir;
          
          /* Check accessed bit */
          if (pagedir_is_accessed(pd, entry->upage))
            {
              /* Give it a second chance */
              pagedir_set_accessed(pd, entry->upage, false);
            }
          else
            {
              /* Found victim */
              victim = entry;
              break;
            }
        }
      
      /* Move clock hand */
      clock_hand = list_next(clock_hand);
      if (clock_hand == list_end(&frame_table))
        clock_hand = list_begin(&frame_table);
      
      iterations++;
    }
  
  if (victim == NULL)
    {
      lock_release(&frame_lock);
      return NULL;
    }
  
  /* Evict the victim frame */
  void *kpage = victim->kpage;
  void *upage = victim->upage;
  struct thread *owner = victim->owner;
  uint32_t *pd = owner->pagedir;
  
  /* Check if page is dirty */
  bool dirty = pagedir_is_dirty(pd, upage);
  
  /* Get supplemental page table entry */
  struct spt_entry *spt_entry = spt_get_entry(&owner->spt, upage);
  
  if (spt_entry != NULL)
    {
      /* Check page type */
      if (spt_entry->type == PAGE_MMAP)
        {
          /* MMAP page - write back if dirty, don't swap */
          if (dirty)
            {
              /* Write page back to file */
              file_seek(spt_entry->file, spt_entry->file_offset);
              file_write(spt_entry->file, kpage, spt_entry->read_bytes);
            }
          /* Mark as not loaded, but keep in SPT for re-loading */
          spt_entry->loaded = false;
          spt_entry->kpage = NULL;
        }
      else if (dirty || spt_entry->writable)
        {
          /* Regular writable page or dirty page - swap out */
          size_t swap_slot = swap_out(kpage);
          spt_set_swap(&owner->spt, upage, swap_slot);
        }
      else
        {
          /* Read-only page can be discarded (reload from file) */
          spt_entry->loaded = false;
          spt_entry->kpage = NULL;
        }
    }
  
  /* Clear page from page table */
  pagedir_clear_page(pd, upage);
  
  /* Move clock hand to next frame */
  clock_hand = list_next(&victim->elem);
  if (clock_hand == list_end(&frame_table))
    clock_hand = list_begin(&frame_table);
  
  /* Remove from frame table */
  list_remove(&victim->elem);
  free(victim);
  
  lock_release(&frame_lock);
  
  return kpage;
}
