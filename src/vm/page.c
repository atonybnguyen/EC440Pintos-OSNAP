#include "vm/page.h"
#include <stdio.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "filesys/file.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "userprog/syscall.h"

static unsigned spt_hash_func(const struct hash_elem *e, void *aux);
static bool spt_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux);
static void spt_destroy_func(struct hash_elem *e, void *aux);

/* Initialize supplemental page table */
void 
spt_init(struct spt *spt)
{
  hash_init(&spt->table, spt_hash_func, spt_less_func, NULL);
  lock_init(&spt->lock);
}

/* Destroy supplemental page table and free all resources */
void 
spt_destroy(struct spt *spt)
{
  lock_acquire(&spt->lock);
  hash_destroy(&spt->table, spt_destroy_func);
  lock_release(&spt->lock);
}

static void check_write_back(struct spt_entry *entry){
  if (entry->type == PAGE_MMAP && entry->loaded && entry->file){
    struct thread *t = thread_current();
    if (pagedir_is_dirty(t->pagedir, entry->upage)){
      lock_acquire(&file_lock);
      file_write_at(entry->file, entry->kpage, entry->read_bytes, entry->file_offset);
      lock_release(&file_lock);
    }
  }
}

/* Add a file-backed page to the supplemental page table */
bool 
spt_set_file(struct spt *spt, void *upage, struct file *file,
             off_t ofs, uint32_t read_bytes, uint32_t zero_bytes,
             bool writable)
{
  struct spt_entry *entry = malloc(sizeof(struct spt_entry));
  if (entry == NULL)
    return false;
  
  entry->upage = pg_round_down(upage);
  entry->kpage = NULL;
  entry->type = PAGE_FILE;
  entry->writable = writable;
  entry->loaded = false;
  entry->file = file;
  entry->file_offset = ofs;
  entry->read_bytes = read_bytes;
  entry->zero_bytes = zero_bytes;
  entry->swap_slot = 0;
  entry->mapid = -1;
  
  lock_acquire(&spt->lock);
  struct hash_elem *result = hash_insert(&spt->table, &entry->elem);
  lock_release(&spt->lock);
  
  if (result != NULL)
    {
      free(entry);
      return false;
    }
  
  return true;
}

/* Add a zero page to the supplemental page table */
bool 
spt_set_zero(struct spt *spt, void *upage, bool writable)
{
  struct spt_entry *entry = malloc(sizeof(struct spt_entry));
  if (entry == NULL)
    return false;
  
  entry->upage = pg_round_down(upage);
  entry->kpage = NULL;
  entry->type = PAGE_ZERO;
  entry->writable = writable;
  entry->loaded = false;
  entry->file = NULL;
  entry->file_offset = 0;
  entry->read_bytes = 0;
  entry->zero_bytes = PGSIZE;
  entry->swap_slot = 0;
  entry->mapid = -1;
  
  lock_acquire(&spt->lock);
  struct hash_elem *result = hash_insert(&spt->table, &entry->elem);
  lock_release(&spt->lock);
  
  if (result != NULL)
    {
      free(entry);
      return false;
    }
  
  return true;
}

/* Add a memory-mapped page to the supplemental page table */
bool 
spt_set_mmap(struct spt *spt, void *upage, struct file *file,
             off_t ofs, uint32_t read_bytes, uint32_t zero_bytes,
             int mapid)
{
  struct spt_entry *entry = malloc(sizeof(struct spt_entry));
  if (entry == NULL)
    return false;
  
  entry->upage = pg_round_down(upage);
  entry->kpage = NULL;
  entry->type = PAGE_MMAP;
  entry->writable = true;  /* MMAP pages are always writable */
  entry->loaded = false;
  entry->file = file;
  entry->file_offset = ofs;
  entry->read_bytes = read_bytes;
  entry->zero_bytes = zero_bytes;
  entry->swap_slot = 0;
  entry->mapid = mapid;
  
  lock_acquire(&spt->lock);
  struct hash_elem *result = hash_insert(&spt->table, &entry->elem);
  lock_release(&spt->lock);
  
  if (result != NULL)
    {
      free(entry);
      return false;
    }
  
  return true;
}

/* Mark a page as loaded with its kernel page */
bool 
spt_set_loaded(struct spt *spt, void *upage, void *kpage)
{
  lock_acquire(&spt->lock);
  struct spt_entry *entry = spt_get_entry(spt, upage);
  if (entry == NULL)
    {
      lock_release(&spt->lock);
      return false;
    }
  
  entry->kpage = kpage;
  entry->loaded = true;
  lock_release(&spt->lock);
  
  return true;
}

/* Get supplemental page table entry for a user page */
struct spt_entry *
spt_get_entry(struct spt *spt, void *upage)
{
  struct spt_entry dummy;
  dummy.upage = pg_round_down(upage);
  
  struct hash_elem *e = hash_find(&spt->table, &dummy.elem);
  if (e == NULL)
    return NULL;
  
  return hash_entry(e, struct spt_entry, elem);
}

/* Load a page into memory (called by page fault handler) */
bool 
spt_load_page(struct spt *spt, void *upage)
{
  lock_acquire(&spt->lock);
  
  struct spt_entry *entry = spt_get_entry(spt, upage);
  if (entry == NULL || entry->loaded)
    {
      lock_release(&spt->lock);
      return false;
    }
  
  /* Save entry information before releasing lock */
  enum page_type type = entry->type;
  bool writable = entry->writable;
  void *upage_addr = entry->upage;
  struct file *file = entry->file;
  off_t file_offset = entry->file_offset;
  uint32_t read_bytes = entry->read_bytes;
  uint32_t zero_bytes = entry->zero_bytes;
  size_t swap_slot = entry->swap_slot;
  
  /* CRITICAL: Release lock before frame allocation to avoid deadlock */
  lock_release(&spt->lock);
  
  /* Allocate a frame WITHOUT holding the SPT lock */
  void *kpage = frame_alloc(PAL_USER, upage_addr);
  if (kpage == NULL)
    {
      return false;
    }
  
  /* Load page content based on type */
  bool success = false;
  
  if (type == PAGE_FILE || type == PAGE_MMAP)
    {
      /* Load from file */
      lock_acquire(&file_lock);
      if (read_bytes > 0)
        {
          file_seek(file, file_offset);
          if (file_read(file, kpage, read_bytes) != (int) read_bytes)
            {
              lock_release(&file_lock);
              frame_free(kpage);
              return false;
            }
        }
      lock_release(&file_lock);
      memset(kpage + read_bytes, 0, zero_bytes);
      success = true;
    }
  else if (type == PAGE_ZERO)
    {
      /* Zero page */
      memset(kpage, 0, PGSIZE);
      success = true;
    }
  else if (type == PAGE_SWAP)
    {
      /* Load from swap */
      swap_in(swap_slot, kpage);
      success = true;
    }
  
  if (success)
    {
      /* Install page into page table */
      if (!pagedir_set_page(thread_current()->pagedir, upage_addr, kpage, writable))
        {
          frame_free(kpage);
          return false;
        }
      
      /* Re-acquire lock to update entry */
      lock_acquire(&spt->lock);
      entry = spt_get_entry(spt, upage);
      if (entry != NULL)
        {
          entry->kpage = kpage;
          entry->loaded = true;
        }
      lock_release(&spt->lock);
    }
  else
    {
      frame_free(kpage);
    }
  
  return success;
}

/* Set page to swap */
bool 
spt_set_swap(struct spt *spt, void *upage, size_t swap_slot)
{
  lock_acquire(&spt->lock);
  
  struct spt_entry *entry = spt_get_entry(spt, upage);
  if (entry == NULL)
    {
      lock_release(&spt->lock);
      return false;
    }
  
  entry->type = PAGE_SWAP;
  entry->swap_slot = swap_slot;
  entry->loaded = false;
  entry->kpage = NULL;
  
  lock_release(&spt->lock);
  return true;
}

/* Remove a page from the supplemental page table */
void 
spt_remove_entry(struct spt *spt, void *upage)
{
  lock_acquire(&spt->lock);
  
  struct spt_entry dummy;
  dummy.upage = pg_round_down(upage);
  
  struct hash_elem *e = hash_delete(&spt->table, &dummy.elem);
  if (e != NULL)
    {
      struct spt_entry *entry = hash_entry(e, struct spt_entry, elem);
      
      /* Free swap slot if page is in swap */
      if (entry->type == PAGE_SWAP && entry->swap_slot != 0)
        swap_free(entry->swap_slot);
      
      free(entry);
    }
  
  lock_release(&spt->lock);
}

/* Hash function for supplemental page table */
static unsigned 
spt_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
  const struct spt_entry *entry = hash_entry(e, struct spt_entry, elem);
  return hash_bytes(&entry->upage, sizeof(entry->upage));
}

/* Comparison function for supplemental page table */
static bool 
spt_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  const struct spt_entry *entry_a = hash_entry(a, struct spt_entry, elem);
  const struct spt_entry *entry_b = hash_entry(b, struct spt_entry, elem);
  return entry_a->upage < entry_b->upage;
}

/* Destructor function for supplemental page table */
static void 
spt_destroy_func(struct hash_elem *e, void *aux UNUSED)
{
  struct spt_entry *entry = hash_entry(e, struct spt_entry, elem);
  
  check_write_back(entry);

  /* If page is loaded, free the frame */
  if (entry->loaded && entry->kpage != NULL)
    {
      frame_free(entry->kpage);
      pagedir_clear_page(thread_current()->pagedir, entry->upage);
    }
  
  /* Free swap slot if in swap */
  if (entry->type == PAGE_SWAP && entry->swap_slot != 0)
    swap_free(entry->swap_slot);
  
  free(entry);
}
