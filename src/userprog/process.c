#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

/* Include for Lab 2*/
#include "threads/malloc.h"
#include "threads/synch.h"

#ifdef VM
#include "vm/page.h"
#include "vm/frame.h"
#endif

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
static void parse_args(char *cmd_line, char **argv, int *argc);


static void parse_args (char *cmd_line, char **argv, int *argc)
{
  char *token, *save_ptr;
  *argc = 0;
  
  for (token = strtok_r (cmd_line, " ", &save_ptr); token != NULL;
       token = strtok_r (NULL, " ", &save_ptr))
    {
    argv[*argc] = token;
    (*argc)++;
  }
  argv[*argc] = NULL;
}

/* helper struct to pass args*/
struct exec_data{
  char *fn_copy;
  struct child_process *child;
};

/* helper that finds a child in the current thread's children list*/
static struct child_process *get_child(tid_t tid){
  struct thread *cur = thread_current();
  struct list_elem *e;

  for (e = list_begin (&cur->children); e != list_end (&cur->children);
       e = list_next (e))
    {
      struct child_process *child = list_entry (e, struct child_process, elem);
      if (child->pid == tid)
      {
        return child;
      }
    }
  return NULL;
}

tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  char *prog_name_buf = malloc(strlen (file_name) + 1);
  if (prog_name_buf == NULL) {
      return TID_ERROR;
  }
  strlcpy (prog_name_buf, file_name, strlen (file_name) + 1);
  
  char *thread_name;
  char *save_ptr;
  thread_name = strtok_r(prog_name_buf, " ", &save_ptr);

  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL){
    free(prog_name_buf);
    return TID_ERROR;
  }
  strlcpy (fn_copy, file_name, PGSIZE);

  struct child_process *child = malloc(sizeof(struct child_process));
  if (child == NULL) {
      free(prog_name_buf);
      palloc_free_page(fn_copy);
      return TID_ERROR;
  }

  child->load_status = false;
  sema_init(&child->load_sema, 0);
  child->parent_thread = thread_current();
  child->exit_status = -1;
  child->exited = false;
  sema_init(&child->wait_sema, 0);

  struct exec_data data;
  data.fn_copy = fn_copy;
  data.child = child;

  tid = thread_create (thread_name, PRI_DEFAULT, start_process, &data);

  if (tid == TID_ERROR){
    free(prog_name_buf);
    palloc_free_page (fn_copy);
    free(child);
  }
  else{
    child->pid = tid;
    sema_down(&child->load_sema);

    if (child->load_status == true){
      list_push_back (&thread_current()->children, &child->elem);
    }
    else {
      free(child);
      tid = TID_ERROR;
    }

    free(prog_name_buf);
  }
  return tid;
}

static void
start_process (void *data_)
{
  struct exec_data *data = (struct exec_data *) data_;
  char *file_name = data->fn_copy;
  struct child_process *child = data->child;

  struct intr_frame if_;
  bool success;

  char *argv[128];
  int argc;
  parse_args(file_name, argv, &argc);

  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  success = load (argv[0], &if_.eip, &if_.esp);

  if (success){
    thread_current()->my_record = child;
    
    void *user_argv_ptrs[argc];
    int i;

    for (i = argc - 1; i >= 0; i--) {
        int len = strlen(argv[i]) + 1;
        if_.esp -= len;
        memcpy(if_.esp, argv[i], len);
        user_argv_ptrs[i] = if_.esp;
    }

    uintptr_t esp_val = (uintptr_t) if_.esp;
    int padding = esp_val % 4;
    if (padding != 0) {
        if_.esp -= padding;
        memset(if_.esp, 0, padding);
    }

    if_.esp -= sizeof(void *);
    *((void **) if_.esp) = NULL;

    for (i = argc - 1; i >= 0; i--) {
        if_.esp -= sizeof(void *);
        *((void **) if_.esp) = user_argv_ptrs[i];
    }

    void *user_argv = if_.esp;
    if_.esp -= sizeof(void *);
    *((void **) if_.esp) = user_argv;

    if_.esp -= sizeof(int);
    *((int *) if_.esp) = argc;

    if_.esp -= sizeof(void *);
    *((void **) if_.esp) = 0;
  }

  child->load_status = success;
  sema_up(&child->load_sema);
  palloc_free_page (file_name);

  if (!success){
    thread_exit ();
  }

  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

int
process_wait (tid_t child_tid UNUSED) 
{
  struct thread *cur = thread_current();  
  struct child_process *child = NULL;

  struct list_elem *e;
  
  for (e = list_begin (&cur->children); e != list_end (&cur->children); e = list_next (e))
  {
    struct child_process *c = list_entry (e, struct child_process, elem);
    if (c->pid == child_tid)
    {
      child = c;
      list_remove(&child->elem);
      break;
    }
  }
  
  if (child == NULL) {
    return -1;
  }

  sema_down(&child->wait_sema);
  int exit_status = child->exit_status;

  free(child);

  return exit_status;
}

void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

#ifdef VM
  /* Unmap all memory-mapped files */
  mmap_unmap_all();
  
  /* Free supplemental page table */
  spt_destroy(&cur->spt);
#endif

  struct list_elem *e;
  for (e = list_begin(&cur->children); e != list_end(&cur->children); ) 
    {
      struct child_process *child = list_entry(e, struct child_process, elem);
      e = list_next(e);

      child->parent_thread = NULL; 

      list_remove(&child->elem); 

      if (child->exited) 
      {
        free(child);
      }
    }

  pd = cur->pagedir;
  if (pd != NULL) 
    {
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

void
process_activate (void)
{
  struct thread *t = thread_current ();

  pagedir_activate (t->pagedir);

  tss_update ();
}

/* ELF definitions */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

#define PE32Wx PRIx32
#define PE32Ax PRIx32
#define PE32Ox PRIx32
#define PE32Hx PRIx16

struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_STACK   0x6474e551

#define PF_X 1
#define PF_W 2
#define PF_R 4

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

#ifdef VM
  /* Initialize supplemental page table */
  spt_init(&t->spt);
#endif

  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  if (!setup_stack (esp))
    goto done;

  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  if (!success){
    file_close (file);
  }
  else{
    t->executable = file;
    file_deny_write(file);
  }  
  return success;
}

static bool install_page (void *upage, void *kpage, bool writable);

static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  if (phdr->p_memsz == 0)
    return false;
  
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  if (phdr->p_vaddr < PGSIZE)
    return false;

  return true;
}

static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

#ifndef VM
  /* Original implementation for Lab 2 */
  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
#else
  /* Lab 3: Demand paging implementation */
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Create supplemental page table entry for lazy loading */
      if (!spt_set_file(&thread_current()->spt, upage, file, ofs, 
                        page_read_bytes, page_zero_bytes, writable))
        return false;

      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      ofs += page_read_bytes;
      upage += PGSIZE;
    }
#endif
  return true;
}

static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

#ifndef VM
  /* Original Lab 2 implementation */
  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
#else
  /* Lab 3: Set up initial stack page with demand paging */
  uint8_t *upage = ((uint8_t *) PHYS_BASE) - PGSIZE;
  
  /* Add stack page to supplemental page table */
  if (spt_set_zero(&thread_current()->spt, upage, true))
    {
      /* Allocate the first stack page immediately for command line args */
      kpage = frame_alloc(PAL_USER | PAL_ZERO, upage);
      if (kpage != NULL)
        {
          success = install_page(upage, kpage, true);
          if (success)
            {
              *esp = PHYS_BASE;
              spt_set_loaded(&thread_current()->spt, upage, kpage);
            }
          else
            frame_free(kpage);
        }
    }
#endif
  return success;
}

static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
