#include "vm/mmap.h"
#include <stdio.h>
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "vm/page.h"
#include "vm/frame.h"

static int next_mapid = 1;

/* Initialize MMAP subsystem for a thread */
void 
mmap_init(struct list *mmap_list)
{
  list_init(mmap_list);
}

/* Create a new memory mapping */
int 
mmap_map(void *addr, struct file *file, off_t offset, size_t length)
{
  struct thread *t = thread_current();
  
  /* Allocate mapping structure */
  struct mmap_mapping *mapping = malloc(sizeof(struct mmap_mapping));
  if (mapping == NULL)
    return -1;
  
  mapping->mapid = next_mapid++;
  mapping->file = file;
  mapping->start_addr = addr;
  mapping->page_count = (length + PGSIZE - 1) / PGSIZE;
  
  /* Add lazy-loaded pages to SPT */
  void *upage = addr;
  off_t file_offset = offset;
  size_t remaining = length;
  
  for (size_t i = 0; i < mapping->page_count; i++)
    {
      size_t page_read_bytes = remaining < PGSIZE ? remaining : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
      
      if (!spt_set_mmap(&t->spt, upage, file, file_offset, 
                        page_read_bytes, page_zero_bytes, mapping->mapid))
        {
          /* Failed - clean up already added pages */
          for (size_t j = 0; j < i; j++)
            {
              spt_remove_entry(&t->spt, addr + j * PGSIZE);
            }
          free(mapping);
          return -1;
        }
      
      upage += PGSIZE;
      file_offset += page_read_bytes;
      remaining -= page_read_bytes;
    }
  
  /* Add to thread's mapping list */
  list_push_back(&t->mmap_list, &mapping->elem);
  
  return mapping->mapid;
}

/* Remove a memory mapping */
void 
mmap_unmap(int mapid)
{
  struct thread *t = thread_current();
  struct list_elem *e;
  
  /* Find the mapping */
  for (e = list_begin(&t->mmap_list); e != list_end(&t->mmap_list); e = list_next(e))
    {
      struct mmap_mapping *mapping = list_entry(e, struct mmap_mapping, elem);
      if (mapping->mapid == mapid)
        {
          /* Write back dirty pages to file */
          for (size_t i = 0; i < mapping->page_count; i++)
            {
              void *upage = mapping->start_addr + i * PGSIZE;
              struct spt_entry *entry = spt_get_entry(&t->spt, upage);
              
              if (entry != NULL && entry->loaded)
                {
                  /* Check if page is dirty */
                  if (pagedir_is_dirty(t->pagedir, upage))
                    {
                      /* Write page back to file */
                      file_seek(entry->file, entry->file_offset);
                      file_write(entry->file, entry->kpage, entry->read_bytes);
                    }
                  
                  /* Free the frame and clear page table entry */
                  frame_free(entry->kpage);
                  pagedir_clear_page(t->pagedir, upage);
                }
              
              /* Remove from SPT */
              spt_remove_entry(&t->spt, upage);
            }
          
          /* Close the file */
          file_close(mapping->file);
          
          /* Remove from list and free */
          list_remove(e);
          free(mapping);
          return;
        }
    }
}

/* Remove all memory mappings for current thread */
void 
mmap_unmap_all(void)
{
  struct thread *t = thread_current();
  
  while (!list_empty(&t->mmap_list))
    {
      struct list_elem *e = list_front(&t->mmap_list);
      struct mmap_mapping *mapping = list_entry(e, struct mmap_mapping, elem);
      mmap_unmap(mapping->mapid);
    }
}

/* Get mapping by ID */
struct mmap_mapping *
mmap_get_mapping(int mapid)
{
  struct thread *t = thread_current();
  struct list_elem *e;
  
  for (e = list_begin(&t->mmap_list); e != list_end(&t->mmap_list); e = list_next(e))
    {
      struct mmap_mapping *mapping = list_entry(e, struct mmap_mapping, elem);
      if (mapping->mapid == mapid)
        return mapping;
    }
  
  return NULL;
}
