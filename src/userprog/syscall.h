#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void sys_exit(int status);
extern struct lock file_lock;

#endif /* userprog/syscall.h */
