			+--------------------+
			|        EC 440      |
			| PROJECT 1: THREADS |
			|   DESIGN DOCUMENT  |
			+--------------------+
				   
---- GROUP ----

>> Fill in the names and email addresses of your group members.

FirstName LastName <email@domain.example>
FirstName LastName <email@domain.example>
FirstName LastName <email@domain.example>

---- PRELIMINARIES ----

>> If you have any preliminary comments on your submission, notes for the
>> TAs, or extra credit, please give them here.
 


>> Please cite any offline or online sources you consulted while
>> preparing your submission, other than the Pintos documentation, course
>> text, lecture notes, and course staff.

			     ALARM CLOCK
			     ===========

---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

In timer.c/h
int64_t next_wakeup_tick (global) - this is the global time that is compared against each threads wakeup time
struct list sleeping_list - where threads who are put to sleep go before being woken up for ready list
int64_t wakeup_tick (local) - this acts as each thread's local time to tell itself to wakeup

In thread.c/h
int64_t wakeup_tick (local)
struct list_elem sleepelem - this is an element in the sleep_list
---- ALGORITHMS ----

>> A2: Briefly describe what happens in a call to timer_sleep(),
>> including the effects of the timer interrupt handler.

When timer_sleep is called it detects non-negative time and initializes the time it takes to wake up in wakeup_tick
It then calls thread_sleep (wakeup_tick) which is a helper function in thread.c
thread_sleep gets the current thread and interrupt state and passes the wakeup_tick to the thread's local wakeup_tick var
It adds the thread into the sleep queue ordered by wakeup time in descending order (list_sleep_less_func is the helper method)
if the thread's wakeup tick is less than the next_wakeup_tick which is initially declared as the MAX the next_wakeup_tick is passed the thread's wakeup tick
it then blocks the thread, calls the scheduler, and sets the interrupt state ON again.

>> A3: What steps are taken to minimize the amount of time spent in
>> the timer interrupt handler?
> 
In the timer interrupt handler, it checks every tick if it's the next wake-up tick for the thread. If its false it does nothing and exits
If it does detect that it is on a wakeup_tick it traverses the sleep_list to determine the next thread to wakeup, unblock, and remove from the sleep_list
and add to the ready_list. 
It then passes the wakeup_tick of the next thread to the global next_wakeup_tick, if the queue is empty it sets it to the maximum value so that any thread wakeup time is always less than the max
In the best case its O(1) cause it checks if the tick is a wakeup tick. The average case is O(k) and worst case is O(n) if n means all threads wake on the same tick
and k is a subset of n. 

---- SYNCHRONIZATION ----

>> A4: How are race conditions avoided when multiple threads call
>> timer_sleep() simultaneously?

timer_sleep calls thread_sleep which disables interrupts before it makes any changes to the sleep_list. This way the current thread and only
the current thread has access and can insert in the sleep_list. Afterward interrupts are re-enabled.

>> A5: How are race conditions avoided when a timer interrupt occurs
>> during a call to timer_sleep()?

with the same solutions above, by disabling interrupts the inclusion of the thread in the sleep_list is contained in the critical section.
So that means the timer interrupt cannot occur between the calculations and inclusion into the sleep_list

---- RATIONALE ----

>> A6: Why did you choose this design?  In what ways is it superior to
>> another design you considered?
> 
Another design I considered was utilizing a semaphore design for synchronization. I ultimately decided that disabling interrupts was a better solution
Semaphoric design would require additional overhead for each call and an additional queue that wouldn't scale as well as a simpler more direct design
like crudely disabling interrupts.


			 PRIORITY SCHEDULING
			 ===================

---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

>> B2: Explain the data structure used to track priority donation.
>> Use ASCII art to diagram a nested donation.  (Alternately, submit a
>> .png file.)

---- ALGORITHMS ----

>> B3: How do you ensure that the highest priority thread waiting for
>> a lock, semaphore, or condition variable wakes up first?

>> B4: Describe the sequence of events when a call to lock_acquire()
>> causes a priority donation.  How is nested donation handled?

>> B5: Describe the sequence of events when lock_release() is called
>> on a lock that a higher-priority thread is waiting for.

---- SYNCHRONIZATION ----

>> B6: Describe a potential race in thread_set_priority() and explain
>> how your implementation avoids it.  Can you use a lock to avoid
>> this race?

---- RATIONALE ----

>> B7: Why did you choose this design?  In what ways is it superior to
>> another design you considered?

			  ADVANCED SCHEDULER
			  ==================

---- DATA STRUCTURES ----

>> C1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

Changes in thread.h:
sleepelem: list element for sleeping list.
wakeup_tick: tick to wake sleeping thread.
ready_list, sleeping_list: global ready/sleeping thread lists.
idle_thread: pointer to idle thread.
load_avg: system load average.
thread_set_sleeping(): sleeps current thread for given ticks.
mlfqs_update_load_avg_and_recent_cpu_all(): updates load_avg and recent_cpu.
mlfqs_recompute_priority_all(): recomputes all thread priorities.

Changes in thread.c:
ready_list, sleeping_list, idle_thread, load_avg: global definitions.
thread_mlfqs_tick(): handles per-tick MLFQS updates.

---- ALGORITHMS ----

>> C2: Suppose threads A, B, and C have nice values 0, 1, and 2.  Each
>> has a recent_cpu value of 0.  Fill in the table below showing the
>> scheduling decision and the priority and recent_cpu values for each
>> thread after each given number of timer ticks:

timer  recent_cpu        priority         thread
ticks   A   B   C       A   B   C         to run
-----  --  --  --      --  --  --         ------
 0      0   0   0       63  61  59         A
 4      4   0   0       62  61  59         A
 8      8   0   0       61  61  59         B
12      8   4   0       61  60  59         A
16     12   4   0       60  60  59         B
20     12   8   0       60  59  59         A
24     16   8   0       59  59  59         B
28     16  12   0       59  58  59         C
32     16  12   4       59  58  58         A
36     20  12   4       58  58  58         B


>> C3: Did any ambiguities in the scheduler specification make values
>> in the table uncertain?  If so, what rule did you use to resolve
>> them?  Does this match the behavior of your scheduler?

Yes, the main ambiguity was how to break ties when multiple ready threads share the highest priority. I resolved this by applying a first-come, first-served rule, where the thread that has been waiting longest among those with equal highest priority is chosen. 

>> C4: How is the way you divided the cost of scheduling between code
>> nside and outside interrupt context likely to affect performance?

By keeping scheduling decisions lightweight inside interrupt context (only marking a yield with intr_yield_on_return()) and performing full context switches outside, we minimize interrupt latency and improve overall responsiveness.

---- RATIONALE ----

>> C5: Briefly critique your design, pointing out advantages and
>> disadvantages in your design choices.  If you were to have extra
>> time to work on this part of the project, how might you choose to
>> refine or improve your design?

For MLFQS, our scheduler updates recent_cpu each tick, recomputes priorities every 4 ticks, and refreshes load_avg each second, then reorders the ready list—this keeps priorities responsive to CPU usage and nice values. The trade-off is overhead: recomputing priorities for all threads and reordering a single ready list is O(n) and correctness depends on reliably firing those periodic updates. With more time, we’d make MLFQS simpler and faster by using separate queues for each priority level and combining the priority calculation into one shared function.

>> C6: The assignment explains arithmetic for fixed-point math in
>> detail, but it leaves it open to you to implement it.  Why did you
>> decide to implement it the way you did?  If you created an
>> abstraction layer for fixed-point math, that is, an abstract data
>> type and/or a set of functions or macros to manipulate fixed-point
>> numbers, why did you do so?  If not, why not?

We implemented fixed-point math using simple macros in fixed.h to keep arithmetic fast and avoid floating-point operations in the kernel. This abstraction makes calculations like load average and recent CPU clear, consistent, and easy to maintain.

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
