          +----------------------------+
          | EC 440                     |
          | PROJECT 3a: VIRTUAL MEMORY |
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

>> Please cite any offline or online sources you consulted while
>> preparing your submission, other than the Pintos documentation, course
>> text, lecture notes, and course staff.

          PAGE TABLE MANAGEMENT
          =====================

---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

**In thread.h**
    struct spt spt
    This is the supplementary page table

    struct list mmap_list
    A list of the memory mapping

**page.h**
    enum page_type{PAGE_FILE, PAGE_SWAP, PAGE_ZERO, PAGE_MMAP}
    This enum variable is used to keep track of type of page for loading and evicting pages

    struct spt_entry{}
    Used to store all of the necessary information on a virtual page

**frame.h**
    frame_entry{}
    This structure is used to track the physical frame allocation and thread owning the frame
    
**frame.c**
    static struct list frame_table
    A "list" of all the frames

    static struct list_elem *clock_hand;
    A pointer that we use to implement the clockhand algorith for page management

**swap.cc**
    static struct block *swap_block
    A tool which we use in order to implemnt swapping

    static struct bitmap *swap_table
    Holds a bitmap of all the available swap blocks

**mmap.h**
    mmap_mapping{}
    Use to track a single memory mapping 


---- ALGORITHMS ----

>> A2: In a few paragraphs, describe your code for accessing the data
>> stored in the SPT about a given page.

Our SPT is implemented as a hash table that maps the user's virtual addresses to spt_entry structures. When we are trying to acccess data for a given page, we would have to page align and then create a dummy spt_entry with that address as the key

We then use hash_find() in order to trace back the SPT's hash table using the dummy entry in order to locate the actual entry we need. Once we extract the spt_entry and then access all the metadata regarding the page such as it's type, file information, swap slot, loaded status, and kernel page address.

In order to maintain synchronization, we made sure that the SPT has a lock that must be held when it is modifying entries. Though we did have to resort to using interrupts so that we don't face a deadlock issue.



>> A3: How does your code coordinate accessed and dirty bits between
>> kernel and user virtual addresses that alias a single frame, or
>> alternatively how do you avoid the issue?

We avoid this issue by only accessing pages through user virtual addresses, never through kernel virtual addresses when checking accessed/dirty bits. All memory accesses from user programs go through user virtual addresses, and our eviction algorithm uses pagedir_is_accessed() and pagedir_is_dirty(), which check the page table entry corresponding to the user virtual address.

---- SYNCHRONIZATION ----

>> A4: When two user processes both need a new frame at the same time,
>> how are races avoided?

The race between two user program is avoided using interrupt disabling in frame_alloc() and evict_frame(). When a process needs a frame, it first calls palloc_get_page(). If that fails, evict_frame() is called.

The eviction process disables interrupts before accessing the frame_table, selecting a victim using the clock algorithm, and removing the frame entry. Only after the frame is removed from the table and interrupts are re-enabled do we perform any I/O operations (swap-out or file write-back).

After obtaining a frame (either from palloc or eviction), the frame_entry is created and added to frame_table with interrupts disabled, preventing races.

---- RATIONALE ----

>> A5: Why did you choose the data structure(s) that you did for
>> representing virtual-to-physical mappings?

We had implemented SPT using a hash map because overall it is the most efficient and is a pretty common for it to be implemented this way too. Using a hash table, we can be more efficient with out memory usage and it also allow for faster lookup time during page faults

We had implemented the frame table as a linked list because it is extremely flexible when it comes to inserting/deleting an item within the list. Also when it comes to the clock algorithm, implementing it using linked lists is as easy as just accessing the next node, but if it were to be an array, it would require us to index instead.

          PAGING TO AND FROM DISK
          =======================

---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

**vm/swap.c**:
    #define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)
    Number of disk sectors per page (8 sectors = 4KB)

    static struct block *swap_block;
    Block device for swap partition

    static struct bitmap *swap_table;
    Bitmap tracking which swap slots are in use

**vm/frame.c**:
    static struct list_elem *clock_hand;
    Current position in clock eviction algorithm

**struct frame_entry (frame.h)**:
    bool pinned;
    If true, frame cannot be evicted (used during I/O)

**struct spt_entry (page.h)**:
    size_t swap_slot;
    Index of swap slot if page is swapped out

    bool loaded;
    True if page is currently in physical memory


---- ALGORITHMS ----

>> B2: When a frame is required but none is free, some frame must be
>> evicted.  Describe your code for choosing a frame to evict.

In the case that a frame is required, but none is free, then we would choose a new one using a clock algorithm to decide which frame will get evicted. First though we would have to disable interrupt and check to see if the frame table is empty. If it is empty, then we would point the clock hand at the beginning of the frame table and then iterate through the list to see if current frame has been accessed or not. If it hasn't been accessed, then it will be a candidate to get replaced. If it has been accessed though, then it will clear the accessed bit and the program would continue to the next frame. This algorithm will run until it it finds a frame which shows not accessed


>> B3: When a process P obtains a frame that was previously used by a
>> process Q, how do you adjust the page table (and any other data
>> structures) to reflect the frame Q no longer has?

In order to adjust the page table to reflect that, in evict_frame(), we had cleared Q's page by removing the virtual to physical mapping so then Q can no longer access that frame through it's original address. We would then update Q's SPT entry to show that it is no longer in memoryu to reflect this change as well. This would resolve any issue for when process P obtains that previeously held frame.

---- SYNCHRONIZATION ----

>> B5: Explain the basics of your VM synchronization design.  In
>> particular, explain how it prevents deadlock.  (Refer to the
>> textbook for an explanation of the necessary conditions for
>> deadlock.)

For synchronization, in order to prevent deadlock, we had decided to use interrupt disabling instead for frame and swap operation. We then had used locks for SPT for modifications outside evictions.


>> B6: A page fault in process P can cause another process Q's frame
>> to be evicted.  How do you ensure that Q cannot access or modify
>> the page during the eviction process?  How do you avoid a race
>> between P evicting Q's frame and Q faulting the page back in?

In order to ensure that Q cannot access or modify the page during the eviction process, we had disable interrupt for pagedir_clear_page() to run to completion prior to Q having the chance to modify the page. In order to avoid a race between P evicting Q's frame and Q faulting the page, we disable interrupts so then Q would not be able to execute until the process is done

>> B7: Suppose a page fault in process P causes a page to be read from
>> the file system or swap.  How do you ensure that a second process Q
>> cannot interfere by e.g. attempting to evict the frame while it is
>> still being read in?

We can ensure that by pinning the frame, using a pin flag, so that in the case that there is an eviction, it wouldn't prematurely exits until after the process is done.

>> B8: Explain how you handle access to paged-out pages that occur
>> during system calls.  Do you use page faults to bring in pages (as
>> in user programs), or do you have a mechanism for "locking" frames
>> into physical memory, or do you use some other design?  How do you
>> gracefully handle attempted accesses to invalid virtual addresses?

We use page faults to bring in pages and in order to gracefully handle attempted accesses to invalid virtual addresses, we would do quick check on the address to see if it is valid first, then we would check to see if the pointer is in the SPT, and in the case that you are writing, we have to make suree that the SPT entry is writable as well.

---- RATIONALE ----

>> B9: A single lock for the whole VM system would make
>> synchronization easy, but limit parallelism.  On the other hand,
>> using many locks complicates synchronization and raises the
>> possibility for deadlock but allows for high parallelism.  Explain
>> where your design falls along this continuum and why you chose to
>> design it this way.

Our design takes on more of a simple design end of the synchronization solution continuum, since we use interrupt disabling rather than locks for critical sections. This design overall had resolve the deadlock issue in exchange for power in parallelism which was a test case that had caused us a lot of trouble.


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
