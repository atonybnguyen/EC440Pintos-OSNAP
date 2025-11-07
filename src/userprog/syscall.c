#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
/* Additional Libraries */
#include <string.h>
#include "userprog/process.h"
#include "userprog/pagedir.h" 
#include "threads/vaddr.h"
#include "devices/shutdown.h"    // shutdown_power_off (for halt)
#include "filesys/filesys.h"        /* filesys_create */
#include "filesys/file.h"           /* file_allow_write, file_close */
#include "threads/synch.h"        /* lock_init, lock_acquire, lock_release */



static void syscall_handler (struct intr_frame *);

/* Added a lock for synchronization */
/* The read and write situation probably */
static struct lock file_lock;
typedef int ssize_t;


/* Helper functions Added for Lab2 */
static void sys_exit(int status);
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

static void uaddr_check(const void *u);
static uint32_t uarg(struct intr_frame *f, int i);
static void *uarg_ptr(struct intr_frame *f, int i);
static const char *uarg_cstr(struct intr_frame *f, int i);
static bool valid_urange(const void *uaddr, size_t size);
static bool copy_in(void *kdst, const void *usrc, size_t n);
static ssize_t copy_in_cstr(char *kbuf, const char *ustr, size_t cap);

//Helpers
static struct file *fd_detach(int fd); /* Helper to detach a single from from file_descriptors */
static void fd_close_all(void); /*Close all fds during sys exit*/
static struct file *fd_get(int fd); /* Used to get the file, given fd */

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
}


static void
syscall_handler(struct intr_frame *f) {

  // First validate that f->esp itself is a sane user pointer.
  uaddr_check(f->esp);

  uint32_t no = uarg(f, 0);

  switch (no) {
    case SYS_HALT: {
      sys_halt();
      // no return
      break;
    }

    case SYS_EXIT: {
      int status = (int) uarg(f, 1);
      sys_exit(status);
      break; // not reached
    }

    case SYS_WRITE: {
      int fd = (int) uarg(f, 1);
      const void *ubuf = (const void*) uarg_ptr(f, 2);
      unsigned size = (unsigned) uarg(f, 3);
      // Inside sys_write you must validate/copy the buffer range (chunking ok)
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
      sys_close(fd);                 // void; no f->eax
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
      f-> eax = (uint32_t) sys_filesize(fd);
      break;
    }

    case SYS_SEEK: {
      int fd = (int) uarg(f, 1);
      unsigned position = (unsigned) uarg(f, 2);
      sys_seek(fd, position);
    }

    case SYS_READ: {
      int fd = (int) uarg(f, 1);                   // 1st arg: fd
      void *ubuf = uarg_ptr(f, 2);                 // 2nd arg: user buffer ptr
      unsigned size = (unsigned) uarg(f, 3);       // 3rd arg: size
      f->eax = (uint32_t) sys_read(fd, ubuf, size);
      break;
    }
    
    case SYS_TELL: {
      int fd = (int) uarg(f, 1);
      sys_tell(fd);
       break;
    }
    
    default:
      sys_exit(-1);
      break;
  }
}

static int sys_read (int fd, void *ubuf, unsigned size) {
  if (size == 0) return 0;
  if (fd == 1) return -1;                 // stdout is not readable

  // STDIN: read from keyboard
  if (fd == 0) {
    unsigned i = 0;
    for (; i < size; i++) {
      uint8_t c = input_getc();
      if (!copy_out((uint8_t*)ubuf + i, &c, 1)) sys_exit(-1);
    }
    return (int)i;
  }

  // Regular file
  struct file *f = fd_get(fd);
  if (f == NULL) return -1;

  const size_t CHUNK = 512;
  uint8_t kbuf[CHUNK];
  unsigned total = 0;

  while (total < size) {
    size_t want = size - total;
    if (want > CHUNK) want = CHUNK;

    lock_acquire(&file_lock);
    int n = file_read(f, kbuf, (int)want);
    lock_release(&file_lock);

    if (n < 0) return -1;        // FS error
    if (n == 0) break;           // EOF
    if (!copy_out((uint8_t*)ubuf + total, kbuf, (size_t)n)) sys_exit(-1);
    total += (unsigned)n;
  }
  return (int)total;
}


static void sys_exit(int status){
  struct thread *cur_thread = thread_current();
  cur_thread->exit_status = status;

  printf ("%s: exit(%d)\n", cur_thread -> name, status);

  // close all open FDs 
  fd_close_all();

  // If you deny writes to the executable, re-allow and close it here
  if (cur_thread->executable) {
    lock_acquire(&file_lock);
    file_allow_write(cur_thread->executable);
    file_close(cur_thread->executable);
    lock_release(&file_lock);
    cur_thread->executable = NULL;

  }

  // TODO (later): notify parent/waiters via child-info struct & sema_up()

  thread_exit();  // never returns
}

static void sys_halt(void){
  shutdown_power_off();
}

pid_t sys_exec (const char *cmd_line) {
  if (cmd_line == NULL) sys_exit(-1);

  char kcmd_line[256];                     /* buffer */             
  ssize_t len = copy_in_cstr(kcmd_line, cmd_line, sizeof kcmd_line);
  if (len < 0) sys_exit(-1);                       
  if (kcmd_line[0] == '\0') return -1;                /* empty name */

  pid_t pid = process_execute(kcmd_line);
  return pid;
}

static int sys_wait (pid_t pid){
  return process_wait(pid);
}

static int sys_write(int fd, const void *ubuf, unsigned size) {
  if (fd != 1) return -1;                 // stdout only for now
  if (size == 0) return 0;

  const size_t CHUNK = 512;
  size_t done = 0;
  uint8_t kbuf[CHUNK];

  while (done < size) {
    size_t n = size - done; if (n > CHUNK) n = CHUNK;
    if (!copy_in(kbuf, (const uint8_t*)ubuf + done, n)) sys_exit(-1);
    putbuf((const char*)kbuf, n);
    done += n;
  }
  return (int)done;
}

static bool sys_create(const char *u_file, unsigned initial_size) {
  if (u_file == NULL) sys_exit(-1);

  /* Copy user string into a kernel buffer with a reasonable cap */
  char kname[256];                                   /* cap: 255 bytes + NUL */
  ssize_t len = copy_in_cstr(kname, u_file, sizeof kname);
  if (len < 0) sys_exit(-1);                         /* bad pointer or no NUL */
  if (kname[0] == '\0') return false;                /* empty name not allowed */

  bool ok;
  lock_acquire(&file_lock);
  ok = filesys_create(kname, initial_size);
  lock_release(&file_lock);
  return ok;
}


  static int sys_open(const char *u_file) {
    if (u_file == NULL){ 
      sys_exit(-1);
    }
      
    /* Copy user string into kernel buffer */
    char kname[256];
    int buffersize = sizeof(kname);
    int len = strlen(u_file);
    if (len >= buffersize){
      sys_exit(-1);      //Accessing out of bound
    }
    if (len < 0) sys_exit(-1);                    // Length should never be less than 0
    if (kname[0] == '\0') return -1;              // Name should never be null as well
    strlcpy(kname, u_file, buffersize-1);         //Scary function, need protection from buffer overflow stuff
    kname[buffersize-1] = "\0";                   //Adding null byte to end a string
    
    /* Open the file */
    lock_acquire(&file_lock);
    struct file *f = filesys_open(kname);
    lock_release(&file_lock);
    if (f == NULL) return -1;                  
  
    /* Find an available file descriptor slot */
    struct thread *t = thread_current();
    int fd;
    for (fd = 2; fd < FD_MAX; fd++) {        //Reserving fd 0 and 1 for stdin/out
      if (t->file_descriptors[fd] == NULL) {
        t->file_descriptors[fd] = f;
        return fd;                                 /* success */
      }
    }
    /* In the case the there is no file descriptor left, close */
    lock_acquire(&file_lock);
    file_close(f);
    lock_release(&file_lock);
    return -1;
  }
  
static void sys_close(int fd) {
  if (fd == 0 || fd == 1) return;           // stdin/stdout: no struct file*
  struct file *f = fd_detach(fd);
  if (!f) return;                            // invalid or already closed → no-op
  lock_acquire(&file_lock);
  file_close(f);
  lock_release(&file_lock);
}

static int sys_filesize(int fd){
  if (fd <= 1) return -1;   //Again, fd 0 and 1 reserved for stdin stdout

  struct file *file = fd_get(fd);
  if (file == NULL) return -1;    //Failure to get it

  lock_acquire(&file_lock);
  int size = file_length(file);
  lock_release(&file_lock);

  return size;
}

static void sys_seek(int fd, unsigned position){
  if(fd <= 1) return;      //Ignore stdin and stdout again

  struct file *file = fd_get(fd);
  if (file == NULL) return;   //Failed to get the file

  lock_acquire(&file_lock);
  file_seek(fd, position);
  lock_release(&file_lock);
}

static int sys_tell(int fd){
  if (fd <= 1) return 0;

  struct file *file = fd_get(fd);
  if (file == NULL) return 0;

  lock_acquire(&file_lock);
  int position = file_tell(file);
  lock_release(&file_lock);

  return position;
}

////////////////////////// HELPERS ////////////////////

// Returns true iff ptr is a mapped user address.
static bool valid_uaddr(const void *uaddr) {
  return uaddr != NULL && is_user_vaddr(uaddr) && pagedir_get_page(thread_current()->pagedir, uaddr) != NULL;
}

static inline void uaddr_check(const void *u) {
  if (!valid_uaddr(u)) sys_exit(-1);  // terminate offending process
}

// For pointer args, just return the user pointer after validating the *pointer value* itself.
// You will still validate/copy the pointed-to buffer/string at use time.
static void* uarg_ptr(struct intr_frame *f, int i) {
  uint32_t raw = uarg(f, i);
  if (raw == 0 || raw >= (uint32_t)PHYS_BASE) sys_exit(-1);
  return (void*) raw;
}

static const char* uarg_cstr(struct intr_frame *f, int i) {
  return (const char*) uarg_ptr(f, i);
}

// Validate an entire [uaddr, uaddr + size) range, page by page.
static bool valid_urange(const void *uaddr, size_t size) {
  const uint8_t *ptr = (const uint8_t *)uaddr;
  const uint8_t *end = ptr + size;
  while (ptr < end) {
    if (!valid_uaddr(ptr)) return false;
    // jump to next page (avoid O(size) loops on huge buffers)
    size_t advance = PGSIZE - pg_ofs(ptr);
    if (advance == 0) advance = PGSIZE;
    if (ptr + advance < ptr) return false; // overflow guard
    ptr += advance;
  }
  return size == 0 || valid_uaddr((const uint8_t*)uaddr + size - 1);
}

static bool sys_remove(const char *u_file) {
  if (u_file == NULL) sys_exit(-1);
  char kname[256];
  ssize_t len = copy_in_cstr(kname, u_file, sizeof kname);
  if (len < 0) sys_exit(-1);
  if (kname[0] == '\0') return false;

  lock_acquire(&file_lock);
  bool ok = filesys_remove(kname);
  lock_release(&file_lock);
  return ok;
}

// Copy user -> kernel; returns false on first bad byte/page.
static bool copy_in(void *kdst, const void *usrc, size_t n) {
  if (!valid_urange(usrc, n)) return false;
  memcpy(kdst, usrc, n);
  return true;
}

// Copy kernel -> user; returns false on first bad byte/page.
static bool copy_out(void *udst, const void *ksrc, size_t n) {
  if (!valid_urange(udst, n)) return false;  // also ensures user-vaddr & mapped
  memcpy(udst, ksrc, n);
  return true;
}

// Copy a NUL-terminated string from user into a kernel buffer with a cap.
// Returns length on success (excluding NUL), or -1 on failure.
static ssize_t copy_in_cstr(char *kbuf, const char *ustr, size_t cap) {
  // cap should be a reasonable limit (e.g., PGSIZE) to avoid scanning forever.
  for (size_t i = 0; i < cap; i++) {
    const char *up = ustr + i;
    if (!valid_uaddr(up)) return -1;
    char c = *(volatile const char *)up; // avoid clever compiler moves
    kbuf[i] = c;
    if (c == '\0') return (ssize_t)i;
  }
  return -1; // not NUL-terminated within cap
}

// Fetch a 32-bit arg from user stack at esp + 4*i
static uint32_t uarg(struct intr_frame *f, int i) {
  const void *p = (const uint8_t*) f->esp + 4*i;
  // Validate the 4-byte range (start and end)
  uaddr_check(p);
  uaddr_check((const uint8_t*)p + 3);
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
  if (fd <= 2) return NULL; //Ignore stdin and out

  struct thread *current_thread = thread_current();
  return current_thread->file_descriptors[fd];
}
