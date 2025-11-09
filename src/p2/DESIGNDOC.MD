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

Made a Git build pipeline that runs sanity checks every merge to main. It connects to the docker container stored on github using private keys. It took forever intiially to build the docker image each time this check ran taking nearly 5/6 minutes to run the full pipeline. I cached the docker build processes that would repeat each time a build occured and reduced the time to 2/3 minutes. Once in the container I would direct the actions to both threads and userprog and built pintos from both to ensure any push wouldn't break functionality. I ran into an issue however running make check in both directories because it wouldn't allow me to run pintos headless with the tests, might circle back to it in the future. -anthony

>> Please cite any offline or online sources you consulted while
>> preparing your submission, other than the Pintos documentation, course
>> text, lecture notes, and course staff.

generative ai and a dream

			   ARGUMENT PASSING
			   ================

---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

static void parse_args(char *cmd_line, char **argv, int *argc);
The function takes in the command that the user inputs and adds it to the stack in reverse order


---- ALGORITHMS ----

>> A2: Briefly describe how you implemented argument parsing.  How do
>> you arrange for the elements of argv[] to be in the right order?
>> How do you avoid overflowing the stack page?

In order to implement argument parsing, I had used the strtok_r command and had looped it continuously through the user input, parsing it by the spaces until there aren't any spaces left. The right order is achieved by setting up argv[] which will store everything from left to right and then in start_process(), the input is added onto the stack in reversed order with padding to align it for future items on the stack. In order to avoid overflowing the stack page, we had limited the input to PGSIZE, truncating the rest. 


---- RATIONALE ----

>> A3: Why does Pintos implement strtok_r() but not strtok()?
Pintos implement strtok_r amd mpt strtok() because that is part of the C library which isn't a part of tje 

Pintos implement strtok_r() but not strtok() because strtok() uses a static internal state to keep track of its position within the string being parsed, while strtok_r() requires us to give it that inforamtion. This would allow for multiple threads to parse string without any conflict with each others.


>> A4: In Pintos, the kernel separates commands into a executable name
>> and arguments.  In Unix-like systems, the shell does this
>> separation.  Identify at least two advantages of the Unix approach.

Two advantage of this Unix approach is that in the case that there are issues in the separation, it won't be the kernal crashing which is , but just the shell. Another benefit is that the shell runs with the user's privilege, meaning that attackers can't take advantage of the kernal's root access to do execute malicious commands/code


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


struct thread:
	struct file *file_descriptors[FD_MAX];
The file descriptor table which holds all of the file descriptor

	struct thread *parent;
A pointer to the parent thread

	struct list children;
A list of all the children

	struct file *executable;
a struct used to enforce denying write to an executable file

	struct child_process *my_record;
It holds the information of this thread for parent process to access


struct child_process
      {
      tid_t pid;                      /* Process ID */
      int exit_status;                /* Exit status */
      bool waited;                    /* Already waited or not */
      bool exited;                    /* Has exited or not*/
      struct list_elem elem;          /* List element */

      struct semaphore wait_sema;   /* Semaphore for wait */
      struct semaphore load_sema;   /* Semaphore for load */

      bool load_status;              /* Load success status */
      struct thread *parent_thread; /* Pointer to the parent's thread */
      };

>> B2: Describe how file descriptors are associated with open files.

Whenever a file is open, a file descriptor is assigned to that process. In the case that another process opens the same file, it would have a different file descriptor for that open file as well. The fd 0 and 1 are reserved for stdin and stdout across all processes.


>> Are file descriptors unique within the entire OS or just within a
>> single process?

File descriptor is unique within just a single process

---- ALGORITHMS ----

>> B3: Describe your code for reading and writing user data from the
>> kernel.

For writing user's data, we had first validate that the buffer we are writing to falls within the user space, both the start and end of it. We then would copy the information from the kernel's buffer to the user's buffer. In the case of reading, it is the opposite, except we would first read the file into the kernel's buffer first, and then we would copy it over to the user's buffer. Prior to copying over the data, we would first make sure that the user's buffer falls within the user space, then we would copy the buffer data in chunks of 512 bytes to avoid large stack allocations. In the case that fd is set to 0, we would take the reading from the user's keyboard instead with input_getc() and storing it in the user's buffer. If fd is set to 1, then we would copy the user's buffer to the kernel's buffer and then writing that information to the console.

>> B4: Suppose a system call causes a full page (4,096 bytes) of data
>> to be copied from user space into the kernel.  What is the least
>> and the greatest possible number of inspections of the page table
>> (e.g. calls to pagedir_get_page()) that might result?  What about
>> for a system call that only copies 2 bytes of data?  Is there room
>> for improvement in these numbers, and how much?

>> B5: Briefly describe your implementation of the "wait" system call
>> and how it interacts with process termination.
	 Our sys_wait function calls process_wait and passes the process id as the argument.
	 process_waits retrieves all the children in the current process list and adds them 
	 to the child process lists. If the current process identifier matches the child identifier
	 it removes the child from the child process list. We also used a semaphore data structure
	 to process each child process without synchronization problems. Once the child exits we free
	 the child process and return the exit_status.


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

	In our sys_write, we dedicated valid_urange as our source of validation
	valid urange checks if the user memory region is validly mapped out and if at any point
	the validation fails then the system would exit using sys_exit. copy_in copies the data between
	the user and kernel space. sys_exit performs all the resource application such as 
	closing file descriptors, freeing memory, restoring write access, closing executables,
	notifying parents/waiters, freeing records, and ultimately exiting the thread.
	Buffers are allocated locally and immediately terminate upon error. Locks are only acquired
	when during a necessary file operation and released after the operation finishes.  

	using valid_urange allows checking user memory and prevents reading or writing to invalid memory
	Early termination allows us to protect our memory space from changes and prevents corruption.
	Also by utilizing sys_exit it centralizes all cleanups to that one function and seperates responsiblities with each function. 
		

---- SYNCHRONIZATION ----

>> B7: The "exec" system call returns -1 if loading the new executable
>> fails, so it cannot return before the new executable has completed
>> loading.  How does your code ensure this?  How is the load
>> success/failure status passed back to the thread that calls "exec"?
		We created the child_process structs and load_sema  to manage the processes. The parent thread
		calls exec creating a child_process struct and initilzes the load_sema. The parent creates a new process and blocks itself by calling sema_down ensuring the parent can't operate until the child signals that its process is complete. The child process executes the start process and loads the executable. The child then signals the parent whether loading fails or not with booleans flags and sema. Once the parent is unblocked it checks if the load status is successful and adds the child process to a list and if it doesn't it frees the memory and returns errors.  

>> B8: Consider parent process P with child process C.  How do you
>> ensure proper synchronization and avoid race conditions when P
>> calls wait(C) before C exits?  After C exits?  How do you ensure
>> that all resources are freed in each case?  How about when P
>> terminates without waiting, before C exits?  After C exits?  Are
>> there any special cases?
		We implemented a wait_sema and booleans in the child_process struct. Before C exits, P is immediately blocked until C executes sys_exit and signals the completion to P to unblock it. After C exits first it calls sys_exit calling sema_up when P waits it sema_down and P proceeds. When P terminates without waiting the C becompes an orphan (O). sys_exit checks if there are any orphans and tells any orphan to free itself. If P terminates after C exits, P calls process_exits which determines if the C has already exited and frees the C. In cleanup, we ensured that P successfully frees C after recieving exit statuses. The process_exit also utilizes pagedir_destroy and we also utilized fd_close_all. We had to change exceptions to call sys_exit wwhenever there is a page fault to also unionize all the exit functions.

---- RATIONALE ----

>> B9: Why did you choose to implement access to user memory from the
>> kernel in the way that you did?

We use a validate-before-dereference strategy in userprog/syscall.c: uarg() / uarg_ptr() / uarg_cstr() read raw args from the user stack, then valid_uaddr()/valid_urange() and copy helpers like copy_in() and copy_in_cstr() safely stage data into kernel buffers before any filesystem or kernel logic runs. This avoids kernel panics from bad pointers, keeps locking clean (we never touch user memory while file_lock is held), and trades a bit of overhead for simpler reasoning compared to trapping and recovering in page_fault().

>> B10: What advantages or disadvantages can you see to your design
>> for file descriptors?

Each process has a per-process FD table (file_descriptors[FD_MAX], fds start at 2) with helpers fd_detach() and fd_close_all(); fd=0/1 are special-cased to console input/output and don’t consume table slots. We guard all filesys_*/file_* calls with a single global file_lock, which makes correctness straightforward (no re-entrancy races), gives O(1) lookup, and isolates processes; the downsides are a fixed per-process capacity and serialization across all FS ops due to the coarse lock.

>> B11: The default tid_t to pid_t mapping is the identity mapping.
>> If you changed it, what advantages are there to your approach?

We keep the identity mapping (a process’s pid is its thread tid), so exec() returns the child’s tid and wait() keys off that id; this keeps bookkeeping minimal and reuses existing thread infrastructure. The tradeoff is less flexibility if we later want a separate PID namespace or multiple threads per process; identity mapping conflates “thread id” and “process id,” which could require refactoring in later projects.

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
