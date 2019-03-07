 /*
   brains.c: an implementation of an interpreter for the brains language.

   Copyright (C) 2011 Thomas DiModica <ricinwich@yahoo.com>

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 /*
   This uses green threads to implement brains.
   Language:
       +-<>,.[]!#   As in brainfuck
       :;           As in Toadskin
       {}           Until-Loop (While current cell is zero)
       (|)          If-Else-Fi, as in L00P
       $            Return/End Program
       '`           Break and Continue
       *            Yield Processor
       &            Spawn a Thread
       %            Fork a Process, as in brainfork's 'Y'
       ^            Up, V, on current cell
       _            Down, P, on current cell
       =            NOP
       ~            Swap data segments, as in Weave
       @            Seperate Processes, as in Weave's ';'

   Some hard syntax/semantics:
      Break and continue cannot be used to do funny things with loops and
         procedures: [:A';] is incorrect code.
      Procedures operate properly under recursive change:
         :A--B++;:B:A--;+;A executes as --+++ .
      A forked process looks like a forked thread. The process's private
         memory is a copy of the current data segment of its parent thread,
         after the changes of fork have been applied. The '~' swaps between
         the private memory of the process and the private memory of the
         parent. Primordial processes may or may not have a parent dataspace,
         and if not, '~' is a NOP.
      There is a '^' despite '+' being implemented as atomic so that, in a
         native thread/fork situation, '^' must be atomic, and '+' is sped up
         by it not checking if threads are sleeping on the modified cell.
         No implementation need have '+' wake sleeping threads.

   Some implementation fluff:
      All base process share a system block of memory.
      An attempted call to an undefined function costs zero clock ticks.
         Until defined, function names are considered comments.
      A failed definition of a function costs one tick.
         Code like :+++; will not fail, but won't succeed either.
         This treats dynamic function redefinition as an action.
      The bf command # is supported, and is "free".
      The first ! in the file is considered to be the end of the programs
         and the begining of the input to them.

      There are two schedulers: A thread-fair scheduler and a process-fair
         scheduler. Both have threads as "kernel" scheduling entities, as I
         didn't want to write an internal scheduler for the process scheduler:
         green threads within green threads. This scheduler would have to be
         called every clock tick (More inefficiency). The only difference is
         the behavior during a yield: this implements a different process
         getting scheduled, while the alternative would have a different
         thread from the same process scheduled.

         They are called thread-fair and process-fair for the reason that
         follows: say there are two processes, A and B, with A having one
         thread and B having two threads. Under thread-fair scheduling, and
         no thread yielding or blocking: each thread gets 1/3 of the processor
         time. A gets 1/3 of the processor time and B gets 2/3. Under process-
         fair scheduling: A gets 1/2 and B gets 1/2, so A's thread gets 1/2
         and B's two threads get 1/4 each of the processor time.

         I thought of another green threads on green threads implementation:
         the intra-process scheduler is only called on a yield. So, a thread
         that doesn't yield always has the processor for the process's
         timeslice. I could implement this, but it makes alot of changes for
         something I'm not going to use. I would have to add a way to block
         the whole process when a thread sleeps, or allow the process to run
         while blocked (breaking the green-ness).

      In addition: define INFANTICIDE to get proper process death semantics.
         In this implementation, when all of the threads of a process
         terminate, the process is terminated, but its children live on.
         With INFANTICIDE defined as a compile option, when all of the threads
         of a process terminate, the process is terminated, and all of its
         children are murdered, like UNIX or Windows would do.

   Final thoughts:
      I wanted to implement capabilities for read/write at least, so that
      a thread could spawn another thread that wasn't allowed to print output,
      and trying to do so was a NOP. The added information had to come from
      either the character set, which is already stretched to near its limit,
      or it had to be in the data array. I already dropped pbrain's procedure
      semantics due to them being hard to read, so adding more like it was
      not acceptable.

      Alot of thought went into how this would work. If processes are spun off
      and can't communicate, then there is no reason to have multiple
      processes. The manner of IPC is also important: I considered message
      passing and signaling, but they were messy like pbrain. At one point,
      I considered blocking sends! The final semaphore/shared memory system
      seems nice.
 */
 /*
   Change Log:

      10/3/11
         Fixed semantics of '`'. The loop [-] and [-`] should be equivalent.

      10/1/11
         Fixed '!' being ignored.

      9/30/11 Version 0.2.1 beta
         Moved procedures to be thread-local.
         Changed the pc to be an int *. This would better support... brainsys.
         Changed quanta semantics, made EOF detection better.

      9/29/11 Version 0.2 beta
         New instruction system.
         Break now works correctly.
         Removes: loops at the begining of a program and loops after loops.
            If either are significant, __your program is poorly designed__.

      9/26/11 Version 0.1.3 beta
         Code clean-up: consistent use of NOPROC.
         Static stack allocation. I hope it will be faster.
            It is actually more CORRECT now, too.
            Added a little tail-call improvement.

      9/22/11 Version 0.1.2 beta
         Clean up code to better than brainsys standard.
         New code to handle invalid quanta.
         Better process list handling.

      9/19/11 Version 0.1.1 beta
         Fixed a stupid uninitialized variable bug in doMatch.
         The code SHOULD NOT have worked before.

         Fixed a memory leak in thread death.

      9/15/11 Version 0.1 beta release
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>



#define DEFAULTQUANTA 10

#define DMEM 65536
#define DMASK (DMEM - 1)
#define IMEM (1<<24)

#define IMASK 255
#define SHIFT 8

#define SCHEDULE_PROCESS 1
#define SCHEDULE_THREAD 2

#define NOPROC -1
#define NUMPROC 62

#define STACKSIZE 1024

#define GOOD 0
#define BAD -2



 /* Process Control Block */
struct PCB
 {
   struct PCB * next; /* Next Process in the List */

   void * readyList; /* Ready Threads for this Process */

   char * pmem; /* Parent's data Memory segment */
   char * dmem; /* My Data Memory segment */

   int threads;
 };

 /* Thread Control Block */
struct TCB
 {
   struct TCB * next; /* Next Thread */

   struct PCB * par; /* Parent Process */

   int * procs [NUMPROC]; /* Procedure List */

   int * pc; /* Program Counter */
   int dp; /* Data Pointer */

   char * cmem; /* Current Memory segment */

   int * stack [STACKSIZE]; /* Call Stack */
   int sp; /* Stack Pointer */
 };



 /*
   GLOBAL DATA
      The nice thing about a paging OS is that we don't have to pay for
      the memory until we use it.
 */
struct PCB * pListHead = NULL;
struct TCB * tListHead = NULL; /* Only used in per-thread scheduling */
struct PCB * dpListHead = NULL; /* Only used in per-process scheduling */

int  Gimem [IMEM]; /* Global Instruction Memory */
char Gsmem [DMEM]; /* System Memory */

struct TCB * sListHead = NULL;

int scheduler = SCHEDULE_PROCESS;

 /* In Windows, stdin is not a REAL pointer. */
FILE * useIn;



 /*
   Given a character, returns its procedure number.
 */
int procNum (char a)
 {
   if ((a >= '0') && (a <= '9')) return a - '0';
   if ((a >= 'A') && (a <= 'Z')) return a - 'A' + 10;
   if ((a >= 'a') && (a <= 'z')) return a - 'a' + 36;
   return NOPROC;
 }

 /*
   Append list TAIL to list HEAD.
 */
void appendList (void ** head, void * tail)
 {
   if (*head == NULL)
    {
      *head = tail;
    }
   else
    {
      while (*head != NULL) head = *head;
      *head = tail;
    }
   return;
 }

 /*
   Frees a TCB list.
 */
void freeTlist (struct TCB * head)
 {
   if (head->next != NULL) freeTlist(head->next);
   free(head);
   return;
 }

 /*
   Frees a PCB list.
 */
void freePlist (struct PCB * head)
 {
   if (head->next != NULL) freePlist(head->next);
   if (head->readyList != NULL) freeTlist(head->readyList);
   free(head->dmem);
   free(head);
   return;
 }

 /*
   Removes the first item from the list.
 */
void * removeFirst (void ** head)
 {
   void ** c;

   c = *head;

   if (*head != NULL)
    {
      *head = *c;
      *c = NULL;
    }

   return c;
 }

 /*
   Returns 1 if there are no threads that can be run.
 */
int deadLocked (void)
 {
   struct PCB * c;
   c = pListHead;
   while (c != NULL)
    {
      if (c->readyList != NULL) return 0;
      c = c->next;
    }
   return 1;
 }

#ifdef INFANTICIDE
 /*
   Purge a thread list of all threads whose parent is TPAR.
 */
void purge (struct TCB ** head, struct PCB * tpar)
 {
   struct TCB * cur, * last;

   last = NULL;
   cur = *head;
   while (cur != NULL)
    {
      if (cur->par == tpar)
       {
         if (last == NULL) *head = (*head)->next;
         else last->next = cur->next;
         cur->next = NULL;
         free(cur);
         cur = last;
       }
      last = cur;
      if (cur == NULL)
         cur = *head;
      else
         cur = cur->next;
    }
   return;
 }

 /*
   Recursive Infanticide. Kill all of the children of tpar.
 */
void recInfanticide (struct PCB * tpar)
 {
   struct PCB * cur, * last;

   last = NULL;
   cur = pListHead;
   while (cur != NULL)
    {
      if (cur->pmem == tpar->dmem)
       {
         if (cur->readyList != NULL) freeTlist(cur->readyList);
         purge(&tListHead, cur);
         purge(&sListHead, cur);

         if (last == NULL) pListHead = pListHead->next;
         else last->next = cur->next;
         cur->next = NULL;

         free(cur->dmem);
         free(cur);

         cur = last;
       }
      last = cur;
      if (cur == NULL)
         cur = pListHead;
      else
         cur = cur->next;
    }
   return;
 }
#endif

 /*
   Kill a process who has no more threads.
      The process scheduler has a more efficient way of doing this.
 */
void makeDead (struct PCB * me)
 {
   struct PCB * last, * cur;

   if (scheduler == SCHEDULE_THREAD)
    {
      last = NULL;
      cur = pListHead;

      while (cur != me)
       {
         last = cur;
         cur = cur->next;
       }

      if (last == NULL) pListHead = pListHead->next;
      else last->next = cur->next;
      cur->next = NULL;

#ifdef INFANTICIDE
      recInfanticide(cur);
      free(cur->dmem);
      free(cur);
#else
      appendList(&dpListHead, cur);
#endif
    }

   return;
 }

 /*
   Get the next scheduled thread.
   The process scheduler removes dead processes to improve efficiency.
 */
struct TCB * getNextThread (void)
 {
   static struct PCB * lp;
   struct PCB * a;
   struct TCB * b;

   if (scheduler == SCHEDULE_PROCESS)
    {
      if ((lp != NULL) && (lp->threads == 0))
       {
#ifdef INFANTICIDE
         recInfanticide(lp);
         free(lp->dmem);
         free(lp);
#else
         appendList(&dpListHead, lp);
#endif
       }
      else
         appendList(&pListHead, lp);

      if (deadLocked()) return NULL;

      while (1)
       {
         a = removeFirst(&pListHead);

         if (a == NULL) return NULL;

         if (a->readyList == NULL)
            appendList(&pListHead, a);
         else
            break;
       }

      b = removeFirst(&(a->readyList));
      lp = a;
    }
   else
    {
      b = removeFirst(&tListHead);
    }
   return b;
 }

/*
   Schedules a thread to run.
*/
void schedule (struct TCB * me)
 {
   if (scheduler == SCHEDULE_PROCESS)
      appendList(&(me->par->readyList), me);
   else
      appendList(&tListHead, me);
   return;
 }

/*
   Creates a thread and schedules it.
*/
int createThread
   (struct PCB * npar, int ** pr, int * npc, int ndp,
    char * ncmem, int ** ns, int nsp)
 {
   struct TCB * c;
   int i;

   c = malloc(sizeof(struct TCB));

   if (c != NULL)
    {
      npar->threads++;

      c->next = NULL;

      c->par = npar;

      if (pr == NULL)
         for (i = 0; i < NUMPROC; i++) c->procs[i] = NULL;
      else
         memcpy(c->procs, pr, NUMPROC * sizeof(int));

      c->pc = npc;
      c->dp = ndp;

      c->cmem = ncmem;

      if (ns != NULL) memcpy(c->stack, ns, STACKSIZE * sizeof(int));

      c->sp = nsp;

      schedule(c);
    }

   return (c == NULL);
 }

/*
   Creates a process, creates its thread,
       and adds it to the process list only if both succeeded.

      Returns 0 on success and 1 on failure.
*/
int createProcess
   (char * copymem, char * npmem, int ** nprocs,
    int * npc, int ndp, int ** ns, int nsp)
 {
   struct PCB * c;

   c = malloc(sizeof(struct PCB));

   if (c != NULL)
    {
      c->next = NULL;

      c->pmem = npmem;
      c->dmem = malloc (DMEM * sizeof(char));

      if (c->dmem != NULL)
       {
         c->threads = 0;

         if (!createThread(c, nprocs, npc, ndp, c->dmem, ns, nsp))
          {
            memcpy(c->dmem, copymem, DMEM * sizeof(char));

            appendList(&pListHead, c);
          }
         else
          {
            free(c->dmem);
            free(c);

            c = NULL;
          }
       }
      else
       {
         free(c);

         c = NULL;
       }
    }

   return (c == NULL);
 }

/*
   Searches the list of threads waiting on semaphores to see if the increment
   was on their semaphore. Yes, this is hella inefficient.
*/
void checkSemaphores (char * mem, int ptr)
 {
   struct TCB * this, * last;

   last = NULL;
   this = sListHead;
   while (this != NULL)
    {
      if ((this->dp == ptr) && (this->cmem == mem))
       {
         if (last == NULL) sListHead = sListHead->next;
         else last->next = this->next;
         this->next = NULL;
         schedule(this);
         return;
       }
      last = this;
      this = this->next;
    }
   return;
 }

/*
   Execute a quanta of instructions...
   Return:
      0 Normal
      1 Die
      2 Sleep
*/
int doQuanta (struct TCB * me, int quanta)
 {
   int cost = 1, curc, count, forever;

   if (quanta == 0) forever = 1;
   else forever = 0;

   while (forever || (quanta > 0))
    {

      curc = *me->pc;
      me->pc++;

#ifdef DEBUG
      fprintf(stderr, "%p : %c  %d\n", me, curc & IMASK, curc >> SHIFT);
#endif

      switch (curc & IMASK)
       {
         case '+':
            me->cmem[me->dp] += curc >> SHIFT;
            break;

         case '-':
            me->cmem[me->dp] -= curc >> SHIFT;
            break;

         case '>':
            me->dp = (me->dp + (curc >> SHIFT)) & DMASK;
            break;

         case '<':
            me->dp = (me->dp - (curc >> SHIFT)) & DMASK;
            break;

         case '.':
            curc >>= SHIFT;
            while (curc--)
               fputc(me->cmem[me->dp], stdout);
            break;

         case ',':
            count = curc >> SHIFT;
            while (count--)
             {
               curc = fgetc(useIn);
               if (curc != EOF) me->cmem[me->dp] = curc;
             }
            break;

         case '[':
         case '(':
            if (me->cmem[me->dp] == 0)
               me->pc += curc >> SHIFT;
            break;

         case '}':
            if (me->cmem[me->dp] == 0)
               me->pc -= curc >> SHIFT;
            break;

         case ']':
            if (me->cmem[me->dp] != 0)
               me->pc -= curc >> SHIFT;
            break;

         case '{':
            if (me->cmem[me->dp] != 0)
               me->pc += curc >> SHIFT;
            break;

         case ':':
            count = procNum(*me->pc);
            if (count != NOPROC)
               me->procs[count] = me->pc + 1;

         case '|':
            me->pc += curc >> SHIFT;
            break;

         case '&':
            me->cmem[me->dp] = 0;
            me->cmem[(me->dp + 1) & DMASK] = 1;
            if (createThread(me->par, me->procs, me->pc, (me->dp + 1) & DMASK,
                             me->cmem, me->stack, me->sp))
               me->cmem[(me->dp + 1) & DMASK] = 0;
            break;

         case '%':
            me->cmem[me->dp] = 0;
            me->cmem[(me->dp + 1) & DMASK] = 1;
            if (createProcess(me->cmem, me->par->dmem, me->procs, me->pc,
                              (me->dp + 1) & DMASK, me->stack, me->sp))
               me->cmem[(me->dp + 1) & DMASK] = 0;
            break;

         case '^':
            count = curc >> SHIFT;
            me->cmem[me->dp] += count;
            while (count--)
               if (sListHead != NULL)
                  checkSemaphores(me->cmem, me->dp);
            break;

         case '_':
            if (me->cmem[me->dp] < (curc >> SHIFT))
             {
               me->pc--; /* Re-try the down. */
               return 2;
             }
            else
               me->cmem[me->dp] -= curc >> SHIFT;
            break;

         case '*':
            return 0;

         case '@':
            return 1;

         case ')':
            break;

         case '=':
            cost = curc >> SHIFT;
            break;

         case '"':
            me->cmem[me->dp] = 0;
            break;

         case '~':
            if (me->cmem == me->par->pmem)
               me->cmem = me->par->dmem;
            else if (me->par->pmem != NULL) /* If I don't want smem */
               me->cmem = me->par->pmem;
            break;

         case ';':
            if (me->sp == STACKSIZE)
               return 1;
            else
               me->pc = me->stack[me->sp++];
            break;

         case '#':
            cost = 0;
            printf("\npc: %d\ndp: %d\nticks: %d\ndata:",
               me->pc - Gimem, me->dp, quanta);
            for (curc = 0; curc < 16; curc++)
               printf(" %02x", me->cmem[(me->dp + curc) & DMASK]);
            putchar('\n');
            break;

         default:
            curc = procNum(curc);
            if ((curc != NOPROC) && (me->procs[curc] != NULL))
             {
               if ((*me->pc == ';') || (*me->pc == '$'))
                  me->pc = me->procs[curc];
               else if (me->sp == 0)
                  fprintf(stderr, "err: no mem for call\n");
               else
                {
                  me->stack[--me->sp] = me->pc;
                  me->pc = me->procs[curc];
                }
             }
            else
               cost = 0;
            break;
       }

      quanta -= cost;
    }

   return 0;
 }

/*
   Execute the current state.
*/
void execute (int quanta)
 {
   struct TCB * curt;
   int c;

   curt = getNextThread();

   while (curt != NULL)
    {
      if (quanta < 0)
         c = (rand() & 127) + 1;
      else
         c = quanta;

      c = doQuanta(curt, c);

      switch (c)
       {
         case 0: // Normal time-out or yielded processor
            schedule(curt);
            break;

         case 1: // Thread died
            curt->par->threads--;
            if (curt->par->threads == 0)
               makeDead(curt->par);
            free(curt);
            break;

         case 2: // Thread blocked: put to sleep
            appendList(&sListHead, curt);
            break;
       }

      curt = getNextThread();
    }

   return;
 }

 /*
   An ungetc wrapper.
 */
void unGetNext (int c, FILE * fin)
 {
   if (c != EOF) ungetc(c, fin);
   return;
 }

 /*
   A getc hack for this program. It filters out the crap.
 */
int getNext (FILE * fin)
 {
   static int done = BAD;
   int c, v;

   if (done == GOOD) return EOF;

   v = BAD;
   while (v == BAD)
    {
      c = fgetc(fin);
      switch (c)
       {
         case EOF:
            done = GOOD;

         case '+': case '-': case '<': case '>': case '.': case ',':
         case '[': case ']': case '{': case '}': case '(': case '|':
         case ')': case ':': case ';': case '$': case '`': case '\'':
         case '^': case '_': case '%': case '&': case '#': case '~':
         case '*': case '@': case '=': case '!':
         case '0': case '1': case '2': case '3': case '4': case '5':
         case '6': case '7': case '8': case '9':
         case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
         case 'G': case 'H': case 'I': case 'J': case 'K': case 'L':
         case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':
         case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
         case 'Y': case 'Z':
         case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
         case 'g': case 'h': case 'i': case 'j': case 'k': case 'l':
         case 'm': case 'n': case 'o': case 'p': case 'q': case 'r':
         case 's': case 't': case 'u': case 'v': case 'w': case 'x':
         case 'y': case 'z':
            v = GOOD;
       }
    }

   return c;
 }

 /*
   backFills break and continue statements.
   Chaining the breaks recursively seemed like a good idea, but didn't
   actually work with breaks interleaved with if statements.
 */
void backFill (int * mimem, int start, int end)
 {
   while (start < end)
    {
      if (mimem[start] == '\'')
         mimem[start] = '|' | (end - start - 1) << SHIFT;
      else if (mimem[start] == '`')
         mimem[start] = '|' | (end - start - 2) << SHIFT;
      start++;
    }
 }

 /*
   The recursive compiler, built from the recursive matcher!
 */
int recCompile (int * mimem, FILE * fin, int cp, int ll, int * pi)
 {
   int op, np, cc, rl, bf;

   bf = 0;
   op = cp - 1;
   while (1)
    {
      cc = getNext(fin);

      if (strchr("+-><^_,.~=", cc) != NULL)
       {
         rl = 1;
         np = getNext(fin);
         while (np == cc)
          {
            rl++;
            np = getNext(fin);
          }
         unGetNext(np, fin);
       }
      else
         rl = 0;

      mimem[cp++] = cc | (rl << SHIFT);
      switch (cc)
       {
         case '~':
            if ((rl & 1) == 0)
               cp--;
            break;

         case '$':
            mimem[cp - 1] = ';';
            break;

         case '[':
            np = recCompile(mimem, fin, cp, cp, NULL);
            if (np == BAD) return BAD;
            mimem[cp - 1] |= (np - cp) << SHIFT;
            mimem[np - 1] |= (np - cp) << SHIFT;
            if ((cp == 1) || ((mimem[cp - 2] & IMASK) == ']') ||
                (mimem[cp - 2] == '"') || (mimem[cp - 2] == '@'))
               cp--;
            else if (((cp + 2) == np) && (mimem[cp] == ('-' | (1 << SHIFT))))
               mimem[cp - 1] = '"';
            else
               cp = np;
            break;

         case '{':
            np = recCompile(mimem, fin, cp, cp, NULL);
            if (np == BAD) return BAD;
            mimem[cp - 1] |= (np - cp) << SHIFT;
            mimem[np - 1] |= (np - cp) << SHIFT;
            if ((cp != 1) && ((mimem[cp - 2] & IMASK) == '}'))
               cp--;
            else
               cp = np;
            break;

         case '(':
            np = recCompile(mimem, fin, cp, ll, &bf);
            if (np == BAD) return BAD;
            cp = np;
            break;

         case ':':
            np = recCompile(mimem, fin, cp, BAD, NULL);
            if (np == BAD) return BAD;
            cp = np;
            break;

         case ']':
            if (mimem[op] != '[') return BAD;
            if (bf) backFill(mimem, op + 1, cp);
            return cp;

         case '}':
            if (mimem[op] != '{') return BAD;
            if (bf) backFill(mimem, op + 1, cp);
            return cp;

         case '|':
            if (mimem[op] != '(') return BAD;
            mimem[op] |= (cp - op - 1) << SHIFT;
            op = cp - 1;
            break;

         case ')':
            if ((mimem[op] != '(') && (mimem[op] != '|')) return BAD;
            cp--;
            mimem[op] |= (cp - op - 1) << SHIFT;
            if (bf) *pi |= bf;
            return cp;

         case ';':
            if (mimem[op] != ':') return BAD;
            mimem[op] |= (cp - op - 1) << SHIFT;
            return cp;

         case '`':
            if (ll == BAD) return BAD;
            bf |= 1;
            break;

         case '\'':
            if (ll == BAD) return BAD;
            bf |= 1;
            break;

         case '@':
         case '!':
         case EOF:
            if ((op == -1) || (mimem[op] == '@'))
             {
               if (cc == EOF) mimem[cp - 1] = '@';
               return cp;
             }
            else
               return BAD;
       }
    }
 }

 /*
   Takes the input and creates the instruction space from it.
   Returns 1 on success and 0 on failure (BACKWARDS!).
 */
int Compile (int * mimem, FILE * fin, FILE ** useMe, char * tsmem)
 {
   int cp, np;

   cp = 0;
   while (!feof(fin))
    {
      if (createProcess(tsmem, tsmem, NULL, mimem + cp, 0, NULL, STACKSIZE))
         fprintf(stderr, "err: no mem for new process\n");
      np = recCompile(mimem, fin, cp, BAD, NULL);
      if (np == BAD) return 0;
      cp = np;
      if (mimem[cp - 1] == '!')
       {
         mimem[cp - 1] = '@';
         *useMe = fin;
         break;
       }
    }

#ifdef DEBUG
   for (cp = 0; cp < np; cp++)
      fprintf(stderr, "%c %d\n", mimem[cp] & IMASK, mimem[cp] >> SHIFT);
#endif

   return 1;
 }

int main (int argc, char ** argv)
 {
   FILE * fin;
   int quantum = DEFAULTQUANTA;
   char ** narg;

   if (argc < 2)
    {
      fprintf(stderr, "usage: brains [-qQ i] files ...\n");
      return 0;
    }

   useIn = stdin;
   srand(time(NULL));

   narg = argv + 1;
   if (argv[1][0] == '-')
    {
      if ((argv[1][1] == 'q') || (argv[1][1] == 'Q'))
       {
         if (argv[1][1] == 'Q') scheduler = SCHEDULE_THREAD;

         if ((argv[1][2] >= '0') && (argv[1][2] <= '9'))
          {
            quantum = atoi(argv[1] + 2);
            narg = argv + 2;
          }
         else
          {
            quantum = atoi(argv[2]);
            narg = argv + 3;
          }
       }
      else
       {
         fprintf(stderr, "unsupported option: \"%s\"\n", argv[1]);
         return 1;
       }
    }

   while (*narg != NULL) /* I know: I shouldn't make this assumption. */
    {
      fin = fopen(*narg, "r");

      if (fin == NULL)
       {
         fprintf(stderr, "cannot open \"%s\"\n", *narg);
         narg++;
         continue;
       }

      if (Compile(Gimem, fin, &useIn, Gsmem))
       {
         memset(Gsmem, '\0', DMEM * sizeof(char));
         execute(quantum);
       }
      else
         fprintf(stderr, "err: \"%s\": code not syntactically correct\n",
               *narg);

      if (useIn != stdin) useIn = stdin;

      fclose(fin);

      if (pListHead != NULL) freePlist(pListHead);
      pListHead = NULL;

      if (dpListHead != NULL) freePlist(dpListHead);
      dpListHead = NULL;

      if (tListHead != NULL) freeTlist(tListHead);
      tListHead = NULL;

      if (sListHead != NULL) freeTlist(sListHead);
      sListHead = NULL;

      narg++;
    }

   return 0;
 }
