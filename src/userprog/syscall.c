#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <string.h>
#include "userprog/process.h"
#include "userprog/pagedir.h" 
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "devices/input.h"
#include "filesys/directory.h"
#include "threads/malloc.h"

#ifdef VM
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/mmap.h"
#endif

static void syscall_handler (struct intr_frame *);
struct lock file_lock;
typedef int ssize_t;
typedef int mapid_t;

/* Helper functions */
void sys_exit(int status);
static void sys_halt(void);
static int sys_write(int fd, const void *buffer, unsigned int size);
static bool sys_create(const char *file, unsigned initial_size);
static bool sys_remove(const char *file);
static int sys_open(const char *u_file);
static void sys_close(int fd);
static pid_t sys_exec(const char *cmd_line);
static int sys_wait(pid_t pid);
static int sys_filesize(int fd);
static void sys_seek(int fd, unsigned position);
static int sys_read (int fd, void *buffer, unsigned size);
static unsigned sys_tell(int fd);

#ifdef VM
static mapid_t sys_mmap(int fd, void *addr);
static void sys_munmap(mapid_t mapid);
#endif

static void uaddr_check(const void *u);
static uint32_t uarg(struct intr_frame *f, int i);
static void *uarg_ptr(struct intr_frame *f, int i);
static const char *uarg_cstr(struct intr_frame *f, int i);
static bool valid_urange(const void *uaddr, size_t size, bool writable);
static bool copy_in(void *kdst, const void *usrc, size_t n);
static ssize_t copy_in_cstr(char *kbuf, const char *ustr, size_t cap);
static struct file *fd_detach(int fd);
static void fd_close_all(void);
static struct file *fd_get(int fd);
static bool copy_out(void *udst, const void *ksrc, size_t n);

#ifdef VM
static void pin_buffer(const void *buffer, size_t size);
static void unpin_buffer(const void *buffer, size_t size);
#endif

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
}

static void
syscall_handler(struct intr_frame *f) 
{
#ifdef VM
  thread_current()->esp_on_syscall = f->esp;
#endif

  uaddr_check(f->esp);
  uint32_t no = uarg(f, 0);

  switch (no) {
    case SYS_HALT:
      sys_halt();
      break;

    case SYS_EXIT:
      sys_exit((int) uarg(f, 1));
      break;

    case SYS_WRITE: {
      int fd = (int) uarg(f, 1);
      const void *ubuf = (const void*) uarg_ptr(f, 2);
      unsigned size = (unsigned) uarg(f, 3);
      f->eax = (uint32_t) sys_write(fd, ubuf, size);
      break;
    }

    case SYS_CREATE: {
      const char *uname = uarg_cstr(f, 1);
      unsigned initial = (unsigned)uarg(f, 2);
      f->eax = (uint32_t)sys_create(uname, initial);
      break;
    }

    case SYS_REMOVE: {
      const char *uname = uarg_cstr(f, 1);
      f->eax = (uint32_t)sys_remove(uname);
      break;
    }

    case SYS_CLOSE: {
      int fd = (int)uarg(f, 1);
      sys_close(fd);
      break;
    }  

    case SYS_EXEC: {
      const char *cmd_line = uarg_cstr(f, 1);
      f->eax = (uint32_t) sys_exec(cmd_line);
      break;
    }
    
    case SYS_OPEN: {
      const char *uname = uarg_cstr(f, 1);
      f->eax = (uint32_t)sys_open(uname);
      break;
    }
      
    case SYS_WAIT: {
      pid_t pid = (pid_t) uarg(f, 1);
      f->eax = (uint32_t) sys_wait(pid);
      break;
    }

    case SYS_FILESIZE: {
      int fd = (int) uarg(f, 1);
      f->eax = (uint32_t) sys_filesize(fd);
      break;
    }

    case SYS_SEEK: {
      int fd = (int) uarg(f, 1);
      unsigned position = (unsigned) uarg(f, 2);
      sys_seek(fd, position);
      break; 
    }

    case SYS_READ: {
      int fd = (int) uarg(f, 1);
      void *ubuf = uarg_ptr(f, 2);
      unsigned size = (unsigned) uarg(f, 3);
      f->eax = (uint32_t) sys_read(fd, ubuf, size);
      break;
    }
    
    case SYS_TELL: {
      int fd = (int) uarg(f, 1);
      f->eax = sys_tell(fd);
      break;
    }

#ifdef VM
    case SYS_MMAP: {
      int fd = (int) uarg(f, 1);
      void *addr = uarg_ptr(f, 2);
      f->eax = (uint32_t) sys_mmap(fd, addr);
      break;
    }

    case SYS_MUNMAP: {
      mapid_t mapid = (mapid_t) uarg(f, 1);
      sys_munmap(mapid);
      break;
    }
#endif

    default:
      sys_exit(-1);
      break;
  }
}

#ifdef VM
/* Map a file into memory */
static mapid_t 
sys_mmap(int fd, void *addr)
{
  /* Validate arguments */
  if (addr == NULL || pg_ofs(addr) != 0)
    return -1;  /* addr must be page-aligned and non-null */
  
  if (fd == 0 || fd == 1)
    return -1;  /* Cannot map stdin or stdout */
  
  if (!is_user_vaddr(addr))
    return -1;
  
  /* Get the file */
  struct file *f = fd_get(fd);
  if (f == NULL)
    return -1;
  
  /* Get file size */
  lock_acquire(&file_lock);
  off_t length = file_length(f);
  lock_release(&file_lock);
  
  if (length == 0)
    return -1;  /* Cannot map empty file */
  
  /* Check that the address range is not already mapped */
  size_t page_count = (length + PGSIZE - 1) / PGSIZE;
  struct thread *t = thread_current();
  
  for (size_t i = 0; i < page_count; i++)
    {
      void *page = addr + i * PGSIZE;
      
      /* Check if page is already in page table */
      if (pagedir_get_page(t->pagedir, page) != NULL)
        return -1;
      
      /* Check if page is already in SPT */
      if (spt_get_entry(&t->spt, page) != NULL)
        return -1;
    }
  
  /* Reopen the file to get independent file descriptor */
  lock_acquire(&file_lock);
  struct file *file_copy = file_reopen(f);
  lock_release(&file_lock);
  
  if (file_copy == NULL)
    return -1;
  
  /* Create the mapping */
  int mapid = mmap_map(addr, file_copy, 0, length);
  
  if (mapid == -1)
    {
      lock_acquire(&file_lock);
      file_close(file_copy);
      lock_release(&file_lock);
    }
  
  return mapid;
}

/* Unmap a file from memory */
static void 
sys_munmap(mapid_t mapid)
{
  if (mapid < 0)
    return;
  
  mmap_unmap(mapid);
}
#endif

/* Remaining syscall implementations... */
static int sys_read (int fd, void *ubuf, unsigned size) {
  if (size == 0) return 0;
  if (fd == 1) return -1;
  if (!valid_urange(ubuf, size, true)) sys_exit(-1);

#ifdef VM
  pin_buffer(ubuf, size);
#endif

  if (fd == 0) {
    unsigned i = 0;
    for (; i < size; i++) {
      uint8_t c = input_getc();
      if (!copy_out((uint8_t*)ubuf + i, &c, 1)) {
#ifdef VM
        unpin_buffer(ubuf, size);
#endif
        sys_exit(-1);
      }
    }
#ifdef VM
    unpin_buffer(ubuf, size);
#endif
    return (int)i;
  }

  struct file *f = fd_get(fd);
  if (f == NULL) {
#ifdef VM
    unpin_buffer(ubuf, size);
#endif
    return -1;
  }

  const size_t CHUNK = 512;
  uint8_t kbuf[CHUNK];
  unsigned total = 0;

  while (total < size) {
    size_t want = size - total;
    if (want > CHUNK) want = CHUNK;

    lock_acquire(&file_lock);
    int n = file_read(f, kbuf, (int)want);
    lock_release(&file_lock);

    if (n < 0) {
#ifdef VM
      unpin_buffer(ubuf, size);
#endif
      return -1;
    }
    if (n == 0) break;
    if (!copy_out((uint8_t*)ubuf + total, kbuf, (size_t)n)) {
#ifdef VM
      unpin_buffer(ubuf, size);
#endif
      sys_exit(-1);
    }
    total += (unsigned)n;
  }

#ifdef VM
  unpin_buffer(ubuf, size);
#endif
  return (int)total;
}

void sys_exit(int status){
  struct thread *cur_thread = thread_current();
  printf ("%s: exit(%d)\n", cur_thread -> name, status);

  fd_close_all();

  if (cur_thread->executable) {
    lock_acquire(&file_lock);
    file_allow_write(cur_thread->executable);
    file_close(cur_thread->executable);
    lock_release(&file_lock);
    cur_thread->executable = NULL;
  }

  if (cur_thread->my_record) 
  {
    struct child_process *child_rec = cur_thread->my_record;
    child_rec->exit_status = status;
    child_rec->exited = true;
    sema_up (&child_rec->wait_sema);
    if (child_rec->parent_thread == NULL) {
      free (child_rec);
    }
    cur_thread->my_record = NULL;
  }

  thread_exit();
}

static void sys_halt(void){
  shutdown_power_off();
}

pid_t sys_exec (const char *cmd_line) {
  uaddr_check(cmd_line);
  char kcmd_line[256];
  ssize_t len = copy_in_cstr(kcmd_line, cmd_line, sizeof kcmd_line);
  if (len < 0) sys_exit(-1);
  if (len == 0) return -1;
  return process_execute(kcmd_line);
}

static int sys_wait (pid_t pid){
  return process_wait(pid);
}

static int sys_write(int fd, const void *ubuf, unsigned size) {
  if (size == 0) return 0;
  if (!valid_urange(ubuf, size, false)) sys_exit(-1);
  if (fd == 0) return -1;

#ifdef VM
  pin_buffer(ubuf, size);
#endif

  if (fd == 1) {
    const size_t CHUNK = 512;
    size_t done = 0;
    uint8_t kbuf[CHUNK];

    while (done < size) {
      size_t n = size - done; 
      if (n > CHUNK) n = CHUNK;
      if (!copy_in(kbuf, (const uint8_t*)ubuf + done, n)) {
#ifdef VM
        unpin_buffer(ubuf, size);
#endif
        sys_exit(-1);
      }
      putbuf((const char*)kbuf, n);
      done += n;
    }
#ifdef VM
    unpin_buffer(ubuf, size);
#endif
    return (int)done;
  }
  
  struct file *f = fd_get(fd);
  if (f == NULL) {
#ifdef VM
    unpin_buffer(ubuf, size);
#endif
    return -1;
  }

  const size_t CHUNK = 512;
  uint8_t kbuf[CHUNK];
  unsigned total = 0;

  while(total < size){
    size_t want = size - total;
    if (want > CHUNK) want = CHUNK;

    if (!copy_in(kbuf, (const uint8_t*)ubuf + total, want)) {
#ifdef VM
      unpin_buffer(ubuf, size);
#endif
      sys_exit(-1);
    }
    lock_acquire(&file_lock);
    int n = file_write(f, kbuf, (int)want);
    lock_release(&file_lock);

    if (n < 0) {
#ifdef VM
      unpin_buffer(ubuf, size);
#endif
      return -1;
    }
    if (n == 0) break;
    total += (unsigned)n;
  }

#ifdef VM
  unpin_buffer(ubuf, size);
#endif
  return (int)total;
}

static bool sys_create(const char *u_file, unsigned initial_size) {
  if (u_file == NULL) sys_exit(-1);
  char kname[NAME_MAX + 1];
  ssize_t len = copy_in_cstr(kname, u_file, sizeof kname);
  if (len == -1) sys_exit(-1);
  if (len == -2) return false;
  if (kname[0] == '\0') return false;

  bool ok;
  lock_acquire(&file_lock);
  ok = filesys_create(kname, initial_size);
  lock_release(&file_lock);
  return ok;
}

static int sys_open(const char *u_file) {
  if (u_file == NULL) sys_exit(-1);
  char kname[256];
  ssize_t len = copy_in_cstr(kname, u_file, sizeof(kname));
  if (len == -1) sys_exit(-1);
  if (len == -2) return -1;
  if (kname[0] == '\0') return -1;
  
  lock_acquire(&file_lock);
  struct file *f = filesys_open(kname);
  lock_release(&file_lock);
  if (f == NULL) return -1;
  
  struct thread *t = thread_current();
  int fd;
  for (fd = 2; fd < FD_MAX; fd++) {
    if (t->file_descriptors[fd] == NULL) {
      t->file_descriptors[fd] = f;
      return fd;
    }
  }
  lock_acquire(&file_lock);
  file_close(f);
  lock_release(&file_lock);
  return -1;
}
  
static void sys_close(int fd) {
  if (fd == 0 || fd == 1) return;
  struct file *f = fd_detach(fd);
  if (!f) return;
  lock_acquire(&file_lock);
  file_close(f);
  lock_release(&file_lock);
}

static int sys_filesize(int fd){
  if (fd <= 1) return -1;
  struct file *file = fd_get(fd);
  if (file == NULL) return -1;
  lock_acquire(&file_lock);
  int size = file_length(file);
  lock_release(&file_lock);
  return size;
}

static void sys_seek(int fd, unsigned position){
  struct file *file = fd_get(fd);
  if (!file) return;
  lock_acquire(&file_lock);
  file_seek(file, position);
  lock_release(&file_lock);
}

static unsigned sys_tell(int fd){
  if (fd <= 1) return 0;
  struct file *file = fd_get(fd);
  if (file == NULL) return 0;
  lock_acquire(&file_lock);
  unsigned position = file_tell(file);
  lock_release(&file_lock);
  return position;
}

/* Helper functions */
static bool valid_uaddr(const void *uaddr, bool writable) {
  if (uaddr == NULL || !is_user_vaddr(uaddr)) return false;

#ifdef VM
  struct thread *t = thread_current();
  struct spt_entry *entry = spt_get_entry(&t->spt, pg_round_down(uaddr));
  
  if (entry != NULL){
    if (writable && !entry->writable) return false;
    return true;
  }
  void *esp = t->esp_on_syscall;
  if (uaddr >= (void*)((uint8_t*)PHYS_BASE - 8*1024*1024) && uaddr < PHYS_BASE) {
      if (uaddr >= (esp - 32)) {
          if (spt_get_entry(&t->spt, pg_round_down(esp)) != NULL) {
             return true; 
          }
      }
  }
  return false;

#else
  void *kpage = pagedir_get_page(thread_current()->pagedir, uaddr);
  if (kpage == NULL) return false;
  return true;

#endif
}

static inline void uaddr_check(const void *u) {
  if (!valid_uaddr(u, false)) sys_exit(-1);
}

static void* uarg_ptr(struct intr_frame *f, int i) {
  uint32_t raw = uarg(f, i);
  if (raw == 0 && !is_user_vaddr((void*)raw)) sys_exit(-1);
  return (void*) raw;
}

static const char* uarg_cstr(struct intr_frame *f, int i) {
  return (const char*) uarg_ptr(f, i);
}

static bool valid_urange(const void *uaddr, size_t size, bool writable) {
  if (uaddr == NULL) return false;
  if (size == 0) return true;

  // Check start of buffer
  if (!valid_uaddr(uaddr, writable)) return false;

  // Check end of buffer 
  const void *end = (const char*)uaddr + size - 1;
  if (!valid_uaddr(end, writable)) return false;
  
  // Check if the buffer wraps around
  //if ((uintptr_t)uaddr > (uintptr_t)end) return false;
  return true;
}

static bool sys_remove(const char *u_file) {
  if (u_file == NULL) sys_exit(-1);
  char kname[256];
  ssize_t len = copy_in_cstr(kname, u_file, sizeof kname);
  if (len == -1) sys_exit(-1);
  if (len == -2) return false;
  if (kname[0] == '\0') return false;

  lock_acquire(&file_lock);
  bool ok = filesys_remove(kname);
  lock_release(&file_lock);
  return ok;
}

static bool copy_in(void *kdst, const void *usrc, size_t n) {
  if (!valid_urange(usrc, n, false)) return false;
  memcpy(kdst, usrc, n);
  return true;
}

static bool copy_out(void *udst, const void *ksrc, size_t n) {
  if (!valid_urange(udst, n, true)) return false;
  memcpy(udst, ksrc, n);
  return true;
}

static ssize_t copy_in_cstr(char *kbuf, const char *ustr, size_t cap) {
  if (!valid_uaddr(ustr, false)) return -1;

  for (size_t i = 0; i < cap; i++) {
    const char *up = ustr + i;
    if (!valid_uaddr(up, false)) return -1;
    char c = *(volatile const char *)up;
    kbuf[i] = c;
    if (c == '\0') return (ssize_t)i;
  }
  return -2;
}

static uint32_t uarg(struct intr_frame *f, int i) {
  const void *p = (const uint8_t*) f->esp + 4*i;
  if (p == NULL || !is_user_vaddr(p)) sys_exit(-1);
  if (!is_user_vaddr((const uint8_t*)p + 3)) sys_exit(-1);
  return *(const uint32_t*) p;
}

static struct file *fd_detach(int fd){
  struct thread *t = thread_current();
  if (fd < 2 || fd >= FD_MAX) return NULL;
  struct file *f = t->file_descriptors[fd];
  t->file_descriptors[fd] = NULL;
  return f;
}

static void fd_close_all(void) {
  struct thread *t = thread_current();
  lock_acquire(&file_lock);
  for (int i = 2; i < FD_MAX; i++) {
    if (t->file_descriptors[i]) {
      file_close(t->file_descriptors[i]);
      t->file_descriptors[i] = NULL;
    }
  }
  lock_release(&file_lock);
}

static struct file *fd_get(int fd){
  if ((fd < 2) || (fd >= FD_MAX)) return NULL;
  struct thread *current_thread = thread_current();
  return current_thread->file_descriptors[fd];
}

#ifdef VM
static void pin_buffer(const void *buffer, size_t size) {
  if (size == 0) return;
  
  struct thread *t = thread_current();
  const void *start = pg_round_down(buffer);
  const void *end = pg_round_down((const uint8_t *)buffer + size - 1);
  
  for (const void *page = start; page <= end; page += PGSIZE) {
    void *kpage = pagedir_get_page(t->pagedir, page);
    if (kpage == NULL) {
      if (!spt_load_page(&t->spt, (void *)page)) {
        continue;
      }
      kpage = pagedir_get_page(t->pagedir, page);
    }
    
    if (kpage != NULL) {
      frame_pin(kpage);
    }
  }
}

static void unpin_buffer(const void *buffer, size_t size) {
  if (size == 0) return;
  
  struct thread *t = thread_current();
  const void *start = pg_round_down(buffer);
  const void *end = pg_round_down((const uint8_t *)buffer + size - 1);
  
  for (const void *page = start; page <= end; page += PGSIZE) {
    void *kpage = pagedir_get_page(t->pagedir, page);
    if (kpage != NULL) {
      frame_unpin(kpage);
    }
  }
}
#endif
