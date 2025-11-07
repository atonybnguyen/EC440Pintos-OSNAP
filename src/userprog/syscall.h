#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

/* Helper struct to remove a file from the fd_table (struct file *file_descriptors[128])*/
static struct file *fd_detach(int fd);

#endif /* userprog/syscall.h */
