#include "vm/swap.h"
#include <bitmap.h>
#include <stdio.h>
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/block.h"

/* Number of sectors per page */
#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

static struct block *swap_block;    /* Swap block device */
static struct bitmap *swap_table;   /* Bitmap of swap slots */
static struct lock swap_lock;       /* Lock for swap table */

/* Initialize the swap table */
void 
swap_init(void)
{
  swap_block = block_get_role(BLOCK_SWAP);
  if (swap_block == NULL)
    return; 
    
  /* Calculate number of pages that fit in swap */
  size_t swap_size = block_size(swap_block) / SECTORS_PER_PAGE;
  
  /* Create bitmap to track free slots */
  swap_table = bitmap_create(swap_size);  
  if (swap_table != NULL)
    bitmap_set_all(swap_table, false);
  
  lock_init(&swap_lock);
}

/* Write a page to swap, returns swap slot index */
size_t 
swap_out(void *kpage)
{
  lock_acquire(&swap_lock);
  
  /* Find free swap slot */
  size_t slot = bitmap_scan_and_flip(swap_table, 0, 1, false);
  if (slot == BITMAP_ERROR){
    lock_release(&swap_lock);
    PANIC("Swap partition is full");
  }
  
  /* Write page to swap (one page = 8 sectors) */
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
    {
      block_sector_t sector = slot * SECTORS_PER_PAGE + i;
      block_write(swap_block, sector, kpage + i * BLOCK_SECTOR_SIZE);
    }
  
  lock_release(&swap_lock);
  
  return slot;
}

/* Read a page from swap */
void 
swap_in(size_t slot, void *kpage)
{
  lock_acquire(&swap_lock);
  
  /* Check if slot is in use */
  if (!bitmap_test(swap_table, slot)){
    lock_release(&swap_lock);
    PANIC("Reading from free swap slot");
  }
  
  /* Read page from swap (one page = 8 sectors) */
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
    {
      block_sector_t sector = slot * SECTORS_PER_PAGE + i;
      block_read(swap_block, sector, kpage + i * BLOCK_SECTOR_SIZE);
    }
  
  /* Free the swap slot */
  bitmap_set(swap_table, slot, false);
  
  lock_release(&swap_lock);
}

/* Free a swap slot */
void 
swap_free(size_t slot)
{
  lock_acquire(&swap_lock);
  
  if (bitmap_test(swap_table, slot))
    bitmap_set(swap_table, slot, false);
  
  lock_release(&swap_lock);
}
