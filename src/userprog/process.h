#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

/* child status struct to track*/
struct child_status {
  pid_t pid;
  struct thread *parent_thread; /* Pointer to the parent's thread */
  bool load_status;
  struct semaphore load_sema;

  int exit_status;
  bool has_exited;
  struct semaphore wait_sema;
  struct list_elem elem;
};

#endif /* userprog/process.h */
