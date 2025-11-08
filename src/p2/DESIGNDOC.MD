		     +--------------------------+
       	       	    |	EC 440		|
		     | PROJECT 2: USER PROGRAMS	|
		     | 	   DESIGN DOCUMENT     	|
		     +--------------------------+

---- GROUP ----

>> Fill in the names and email addresses of your group members.

Hieu Nguyen <hnguyen0@bu.edu>
Anthony Nguyen <thonyngu@bu.edu>
Jeehan Zaman <jeehanz@bu.edu>

---- PRELIMINARIES ----

>> If you have any preliminary comments on your submission, notes for the
>> TAs, or extra credit, please give them here.

>> Please cite any offline or online sources you consulted while
>> preparing your submission, other than the Pintos documentation, course
>> text, lecture notes, and course staff.

			   ARGUMENT PASSING
			   ================

---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.




---- ALGORITHMS ----

>> A2: Briefly describe how you implemented argument parsing.  How do
>> you arrange for the elements of argv[] to be in the right order?
>> How do you avoid overflowing the stack page?

In order to implement argument parsing, I had used the strtok_r command and had looped it continuously through the user input, parsing it by the spaces until there aren't any spaces left. The right order is achieved since strtok_r() returns a pointer to the first token from parsing, so each token is added from left to right. In order to avoid overflowing the stack page, we had used the helper function valid_urange which would check the length of the user's input.

---- RATIONALE ----

>> A3: Why does Pintos implement strtok_r() but not strtok()?
Pintos implement strtok_r amd mpt strtok() because that is part of the C library which isn't a part of tje 


>> A4: In Pintos, the kernel separates commands into a executable name
>> and arguments.  In Unix-like systems, the shell does this
>> separation.  Identify at least two advantages of the Unix approach.

			     SYSTEM CALLS
			     ============

---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

static struct lock file_lock;
We are implementing a lock to ensure synchronization and everything is happening in the correct order

typedef int ssize_t;
A variable which we use to store the size of the user's input

static void sys_exit(int status);
The exit system call for our project

static void sys_halt(void);
The halt system call for the lab

static int sys_write(int fd, const void *buffer, unsigned int size);
The write system call for the lab

static bool sys_create(const char *file, unsigned initial_size);
The create system call for the lab

static bool sys_remove(const char *file);
The remove system call for the lab

static int sys_open(const char *u_file);
The open system call for the lab

static void sys_close(int fd);
The close system call for the lab

static pid_t sys_exec(const char *cmd_line);
The exec system call for the lab

static int sys_wait(pid_t pid);
The wait system call for the lab

static int sys_filesize(int fd);
The file size system call for the lab

static void sys_seek(int fd, unsigned position);
The seek system call for the lab

static int sys_read (int fd, void *buffer, unsigned size);
The read system call for the lab

static unsigned sys_tell(int fd);
The tell system call for the lab

static void uaddr_check(const void *u);
A helper function which checks to see if a given address is valid

static uint32_t uarg(struct intr_frame *f, int i);
A helper function which would hold the user input

static void *uarg_ptr(struct intr_frame *f, int i);
A helper function which point to the argument the user had inputted

static const char *uarg_cstr(struct intr_frame *f, int i);
A helper function that 

static bool valid_urange(const void *uaddr, size_t size);
A helper functon which checks to see if the range of the user input is valid


static bool copy_in(void *kdst, const void *usrc, size_t n);
A helper function which would write the user's input into a buffer

static ssize_t copy_in_cstr(char *kbuf, const char *ustr, size_t cap);
A helper function which would copy in the user input


//Helpers
static struct file *fd_detach(int fd); /* Helper to detach a single from from file_descriptors */

static void fd_close_all(void); 
/*Close all fds during sys exit*/

static struct file *fd_get(int fd); 
/* Used to get the file, given fd */

static bool copy_out(void *udst, const void *ksrc, size_t n);
A helper function used to read from buffer and write it somewhere else

static bool valid_uaddr(const void *uaddr);
A helper function which checks if a address is a valid one

>> B2: Describe how file descriptors are associated with open files.
Whenever a file is open, a file descriptor is assigned to that process. In the case that another process opens the same file, it would receive a different file descriptor as well.

>> Are file descriptors unique within the entire OS or just within a
>> single process?
Unique within the entire OS

---- ALGORITHMS ----

>> B3: Describe your code for reading and writing user data from the
>> kernel.


>> B4: Suppose a system call causes a full page (4,096 bytes) of data
>> to be copied from user space into the kernel.  What is the least
>> and the greatest possible number of inspections of the page table
>> (e.g. calls to pagedir_get_page()) that might result?  What about
>> for a system call that only copies 2 bytes of data?  Is there room
>> for improvement in these numbers, and how much?

>> B5: Briefly describe your implementation of the "wait" system call
>> and how it interacts with process termination.

>> B6: Any access to user program memory at a user-specified address
>> can fail due to a bad pointer value.  Such accesses must cause the
>> process to be terminated.  System calls are fraught with such
>> accesses, e.g. a "write" system call requires reading the system
>> call number from the user stack, then each of the call's three
>> arguments, then an arbitrary amount of user memory, and any of
>> these can fail at any point.  This poses a design and
>> error-handling problem: how do you best avoid obscuring the primary
>> function of code in a morass of error-handling?  Furthermore, when
>> an error is detected, how do you ensure that all temporarily
>> allocated resources (locks, buffers, etc.) are freed?  In a few
>> paragraphs, describe the strategy or strategies you adopted for
>> managing these issues.  Give an example.

---- SYNCHRONIZATION ----

>> B7: The "exec" system call returns -1 if loading the new executable
>> fails, so it cannot return before the new executable has completed
>> loading.  How does your code ensure this?  How is the load
>> success/failure status passed back to the thread that calls "exec"?

>> B8: Consider parent process P with child process C.  How do you
>> ensure proper synchronization and avoid race conditions when P
>> calls wait(C) before C exits?  After C exits?  How do you ensure
>> that all resources are freed in each case?  How about when P
>> terminates without waiting, before C exits?  After C exits?  Are
>> there any special cases?

---- RATIONALE ----

>> B9: Why did you choose to implement access to user memory from the
>> kernel in the way that you did?

>> B10: What advantages or disadvantages can you see to your design
>> for file descriptors?

>> B11: The default tid_t to pid_t mapping is the identity mapping.
>> If you changed it, what advantages are there to your approach?

			   SURVEY QUESTIONS
			   ================

Answering these questions is optional, but it will help us improve the
course in future quarters.  Feel free to tell us anything you
want--these questions are just to spur your thoughts.  You may also
choose to respond anonymously in the course evaluations at the end of
the quarter.

>> In your opinion, was this assignment, or any one of the three problems
>> in it, too easy or too hard?  Did it take too long or too little time?

>> Did you find that working on a particular part of the assignment gave
>> you greater insight into some aspect of OS design?

>> Is there some particular fact or hint we should give students in
>> future quarters to help them solve the problems?  Conversely, did you
>> find any of our guidance to be misleading?

>> Do you have any suggestions for the TAs to more effectively assist
>> students, either for future quarters or the remaining projects?

>> Any other comments?
