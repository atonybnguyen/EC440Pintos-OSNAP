#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stddef.h>

/* Initialize the swap table */
void swap_init(void);

/* Write a page to swap, returns swap slot index */
size_t swap_out(void *kpage);

/* Read a page from swap */
void swap_in(size_t slot, void *kpage);

/* Free a swap slot */
void swap_free(size_t slot);

#endif /* vm/swap.h */
