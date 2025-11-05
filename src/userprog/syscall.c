#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
/* Additional Libraries */
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);

/* Helper functions Added for Lab2 */
static void sys_exit(int status);
static void sys_halt();
static int sys_write(int fd, const void *buffer, unsigned int size);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");

  //Casting the stack pointer at the point of intr into esp
  //Treats the memory stack as an array of int
  int *esp = (int *) f->esp;

  //Getting the syscall
  int syscall = *esp;

  switch(syscall){
    case SYS_EXIT:
      //Exiting the thread from user input
      sys_exit(*(esp+1));
      //Breaking here since we are exiting 
      break;
    case SYS_HALT:
      sys_halt();
      break;
    case SYS_WRITE; //Note: Has 3 arguments
      f -> eax = sys_write(*(esp + 1), *(esp+2), *(esp+3));
      break;
  }
  thread_exit ();
}

static void sys_exit(int status){
  struct thread *cur_thread = thread_current();
  cur_thread->status = status;

  printf ("%s: exit(%d)\n", cur_thread -> name, status);
  thread_exit();
}

static void sys_halt(){
  shutdown_power_off();
}

static int sys_write(int fd, const void *buffer, unsigned int size){
/* for fd = 1, writing to console */
  if (fd = 1){
    /* putbuf writes N characters from the buffer to the console */
    putbuf(buffer,size);
  }
}



