          +----------------------------+
          | EC 440                     |
          | PROJECT 3b: VIRTUAL MEMORY |
          | DESIGN DOCUMENT            |
          +----------------------------+

---- GROUP ----

>> Fill in the names and email addresses of your group members.

Hieu Nguyen <hnguyen0@bu.edu>
Anthony Nguyen <thonyngu@bu.edu>
Jeehan Zaman <jeehanz@bu.edu>

---- PRELIMINARIES ----

>> If you have any preliminary comments on your submission, notes for the
>> TAs, or extra credit, please give them here.
> Running this without configuring proper line endings (especially in windows) causes the mmap tests to fail.
> Before you grade please ensure that (depending on what system you have: windows) that you configure proper line endings
> It messes up the sample.txt for the vm test suite.

>> Please cite any offline or online sources you consulted while
>> preparing your submission, other than the Pintos documentation, course
>> text, lecture notes, and course staff.

          STACK GROWTH
          =====================

---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.
> 
> uint8_t *upage (declared in process.c)
> This is a pointer to the user page. 

---- ALGORITHMS ----

>> A2: Explain your heuristic for deciding whether a page fault for an
>> invalid virtual address should cause the stack to be extended into
>> the page that faulted.
>  When the invalid memory access is within 32 bytes below the current stack pointer
>  the function assumes that it's a valid stack growth request and allocate a new page. 
>  However, in our exception.c we first determine whether the fault is near the stack pointer
>  if so then, we check whether it exceeds the stack size limit and attempts to allocate a new zeroed page
>  Involving the kernel to analyze the instruction that faulted would add considerable overhead to each page fault, costing performance
> 

          MEMORY MAPPED FILES
          ===================

---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

---- ALGORITHMS ----

>> B2: Describe how memory mapped files integrate into your virtual
>> memory subsystem.  Explain how the page fault and eviction
>> processes differ between swap pages and other pages.

>> B3: Explain how you determine whether a new file mapping overlaps
>> any existing segment.

---- RATIONALE ----

>> B4: Mappings created with "mmap" have similar semantics to those of
>> data demand-paged from executables, except that "mmap" mappings are
>> written back to their original files, not to swap.  This implies
>> that much of their implementation can be shared.  Explain why your
>> implementation either does or does not share much of the code for
>> the two situations.

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
