#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "filesys/off_t.h"
#include "threads/synch.h"

/* Page types */
enum page_type 
{
  PAGE_FILE,      /* Page loaded from file */
  PAGE_SWAP,      /* Page swapped to disk */
  PAGE_ZERO       /* Page of all zeros */
};

/* Supplemental page table entry */
struct spt_entry 
{
  void *upage;              /* User virtual address (page-aligned) */
  void *kpage;              /* Kernel virtual address (frame), NULL if not loaded */
  enum page_type type;      /* Type of page */
  bool writable;            /* Whether page is writable */
  bool loaded;              /* Whether page is currently in memory */
  
  /* For file-backed pages */
  struct file *file;        /* File to read from */
  off_t file_offset;        /* Offset in file */
  uint32_t read_bytes;      /* Bytes to read from file */
  uint32_t zero_bytes;      /* Bytes to zero after read */
  
  /* For swapped pages */
  size_t swap_slot;         /* Swap slot index */
  
  struct hash_elem elem;    /* Hash table element */
};

/* Supplemental page table */
struct spt 
{
  struct hash table;        /* Hash table of page entries */
  struct lock lock;         /* Lock for synchronization */
};

/* Initialize supplemental page table */
void spt_init(struct spt *spt);

/* Destroy supplemental page table and free all resources */
void spt_destroy(struct spt *spt);

/* Add a file-backed page to the supplemental page table */
bool spt_set_file(struct spt *spt, void *upage, struct file *file,
                  off_t ofs, uint32_t read_bytes, uint32_t zero_bytes,
                  bool writable);

/* Add a zero page to the supplemental page table */
bool spt_set_zero(struct spt *spt, void *upage, bool writable);

/* Mark a page as loaded with its kernel page */
bool spt_set_loaded(struct spt *spt, void *upage, void *kpage);

/* Get supplemental page table entry for a user page */
struct spt_entry *spt_get_entry(struct spt *spt, void *upage);

/* Load a page into memory (called by page fault handler) */
bool spt_load_page(struct spt *spt, void *upage);

/* Set page to swap */
bool spt_set_swap(struct spt *spt, void *upage, size_t swap_slot);

/* Remove a page from the supplemental page table */
void spt_remove_entry(struct spt *spt, void *upage);

#endif /* vm/page.h */
