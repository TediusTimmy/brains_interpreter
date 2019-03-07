# brains_interpreter
Interpreter for the brains bf derivative.

## BRAINS:

brains is a brainfuck derivative that cobbles together the best parts of many other esoteric languages, then breaks them. The commands are:  

Firstly, all of brainfuck: +-<>,.[]  
\+ increment current cell  
\- decrement current cell  
< move tape left  
\> move tape right  
, input current cell  
. output current cell  
[ while current cell is not zero  
] wend  

Next, add L00P's if-then-else construct: (|)  
( if current cell is not zero  
| else (optional)  
) end if  

Throw in Toadskin's procedure definitions: :;  
: define a named procedure  
; end definition  

And some stuff from Weave: ;~  
@ Weave's ; which separates concurrent instruction tapes  
~ swap data segments, or tapes  

Finish with brainfork: Y  
% brainfork's Y to fork a process  
& this is Y in the two interpreters in the archive  

Finally, add in my own stuff: {}*$^_='\`  
\{ while current cell is zero  
\} wend  
\* yield processor  
$ return  
' break, as in program flow control  
` continue  
= NOP  
^ up, V  
_ down, P  

So, brains is multi-threading, and multi-processing. Like Weave, it allows 
processes to communicate with shared data, and even has semaphores to provide 
locking. Unlike Weave, there is no assumption of thread/process time-slicing. 
Explicit NOPs are given for no good reason. I went with Toadskin's named 
procedures because pbrain's procedures are hard to read. In addition, I 
didn't use ! to define processes due to its traditional use in brainfuck.  


Things that need to be explained, part Easy. $ is like return in C. If one is 
in a procedure, the procedure is exited. If one is in the main process, it 
exits. It's handy sometimes. Like ' and ` are handy. They only work on [] and 
{} loops, and they shouldn't allow funny business (if you know what I mean). 
Finally, * is to allow better thread control than busy-waiting or executing 
NOPs. It's how a nice process doesn't do things for a while.  

Part Medium: ^ and _. I am not a good resource for how to use semaphores, but 
these work as follows. ^ is like +. It increments the current cell on the 
tape, but it does so atomically. It also wakes any thread that was sleeping 
on that cell of the tape. _ is like -. It decrements the current cell on the 
tape, and it does so atomically. However, if the current cell is zero, it 
doesn't decrement the tape, but it puts the current thread to sleep until 
some other thread performs a ^ on the current cell. It won't allow the 
cell on the tape to become negative, and it does so atomically. These are 
very useful operations for controlling access to shared resources.  

Part Hard: % and &, with a little ~ thrown in. So, & spawns a thread. 
The new thread sees the tape offset one cell to the right and the current 
cell in the parent is set to zero while the current cell in the child is set 
to one. Both, however are part of the same process. In contrast, % forks a 
new process, and is like UNIX's fork() function call. Firstly, the current 
cell is set to zero and the next cell to the right is set to one, just like 
in a thread spawn. The difference lies in that the new thread gets a copy of 
the forking thread's current data segment, which constitutes the new 
process's private data segment. The ~ operation, for the new process, swaps 
between its private data segment and the private data segment of its parent 
process (which it may or may not have a copy of), thus allowing IPC. For a 
process involved in the "big bang", all independent processes separated by 
@s in the source file, ~ either is a NOP, or it swaps between the process's 
private memory and a shared system memory. As a note: if a thread of a 
multi-threaded process forks a new process, that new process has only one 
thread. It doe not get copies of all threads.  

Examples:  

This first example shows off the use of semaphores to control access to a 
shared resource, the screen. Each process prints out three characters: 
"HI\n". A semaphore ensures that only one process writes to the screen at 
a time. (This example assumes a shared base memory for "big bang" processes)

\~^@  
++++++++[>+++++++++<-]>>++++++++++<<  \~\_\~>.+.>.\~<<^  @  
++++++++[>+++++++++<-]>>++++++++++<<  \~\_\~>.+.>.\~<<^  

No matter how many copies of the

++++++++[>+++++++++<-]>>++++++++++<<  \~\_\~>.+.>.\~<<^  

process are present, nor the order in which process are run, nor the length 
of time each process is given to run will change the output. There will be as 
many lines with "HI" as there are processes printing it. The first process, 
the simple ~^ process, initializes the semaphore and until it runs, nothing 
will be printed.  

This second example involves a multi-threaded program:

\+>&  

(>>++++[>>++++++++<<-]<<|  
======= ================================ ====== ====== ====== ====== ===)  

\>>  ++++++++[>>+++++++++<<-]++++++++++   <<(<<_>>|<_>)>>>>.+.<<.<<(<)<^  

One thread prints out "hi\n", while the other prints out "HI\n". If these 
threads each execute from one to six instructions per scheduling, the "hi\n" 
thread will print first. Seven or more instructions per scheduling, and the 
"HI\n" thread prints first. What is important is that one-and-only-one thread 
gets to print to the screen at the same time. In contrast, for

\>&  
(>>++++[>>++++++++<<-]<<)  
\>>  ++++++++[>>+++++++++<<-]++++++++++   <<(<<_>>)>>>>.+.<<.<<(<)<^  

the "HI\n" thread ALWAYS prints to the screen first, while in

\>&  
(>>++++[>>++++++++<<-]<<)  
\>>  ++++++++[>>+++++++++<<-]++++++++++   <<(|<_>)>>>>.+.<<.<<(<)<^  

the "hi\n" thread prints to the screen first, ALWAYS.  

One final note: procedures. Procedures can be named [0-9a-zA-Z], so there 
are only 62 of them. Binding is a dynamic occurrence, so :+++; represents a 
failed binding rather than a syntax error. Each procedure begins unbound and 
is considered a comment until binding. Each process has its own procedure 
list, and bindings are process local. During a fork, the child gets a copy of 
the parent process' procedure list. Actually, thread-local procedure lists 
may be more useful.  

These examples show the use of brains in programming multi-threaded, 
multi-processing applications. Have fun and try not to go insane.  

IMPLEMENTATION SPECIFICS:  
Whether there is error detection in fork/spawn.  
Whether there is a globally-shared system memory.  
The scope of procedure binding: global, per-process, or per-thread.  
Behavior when a parent process dies: to kill the children or not.  
How threads/processes are scheduled.  
Everything that is implementation specific to brainfuck.  