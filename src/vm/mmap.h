#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <list.h>
#include "filesys/file.h"

/* Memory mapping structure */
struct mmap_mapping
{
  int mapid;                  /* Mapping ID */
  struct file *file;          /* File being mapped */
  void *start_addr;           /* Starting user virtual address */
  size_t page_count;          /* Number of pages mapped */
  struct list_elem elem;      /* List element for thread's mmap_list */
};

/* Initialize MMAP subsystem for a thread */
void mmap_init(struct list *mmap_list);

/* Create a new memory mapping */
int mmap_map(void *addr, struct file *file, off_t offset, size_t length);

/* Remove a memory mapping */
void mmap_unmap(int mapid);

/* Remove all memory mappings for current thread */
void mmap_unmap_all(void);

/* Get mapping by ID */
struct mmap_mapping *mmap_get_mapping(int mapid);

#endif /* vm/mmap.h */
