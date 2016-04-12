#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"



struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

int
allocpid(void)
{
  int pid;
  // acquire(&ptable.lock);
  // pid = nextpid++;
  // release(&ptable.lock);
  do {
    pid = nextpid;
  } while (!cas(&nextpid, pid, pid + 1));
  return pid;
}
//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  // acquire(&ptable.lock);
  // for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  //   if(p->state == UNUSED)
  //     goto found;
  // release(&ptable.lock);

  do {
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      if(p->state == UNUSED)
        break;
    if (p == &ptable.proc[NPROC])
      return 0; // ptable is full
  } while (!cas(&p->state, UNUSED, EMBRYO));

  // return 0;

// found:
  // p->state = EMBRYO;
  // release(&ptable.lock);

  p->pid = allocpid();

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;
  p->sighandler = (sig_handler)-1; // default signal handler value
  p->cstack.head = 0;
  struct cstackframe *newSig;
  for(newSig = p->cstack.frames ;  newSig < p->cstack.frames + 10; newSig++){
    newSig->used = 0;
  }
  p->ignoreSignals=0;
  p->sigPauseInvoked = 0;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;

  sz = proc->sz;
  if(n > 0){
    if((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  proc->sz = sz;
  switchuvm(proc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from p.
  if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = proc->sz;
  np->parent = proc;
  *np->tf = *proc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);
  np->sighandler = proc->sighandler; // copy the parent's signal handler
  safestrcpy(np->name, proc->name, sizeof(proc->name));

  pid = np->pid;

  // lock to force the compiler to emit the np->state write last.
  acquire(&ptable.lock);
  np->state = RUNNABLE;
  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *p;
  int fd;

  if(proc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd]){
      fileclose(proc->ofile[fd]);
      proc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(proc->cwd);
  end_op();
  proc->cwd = 0;

  acquire(&ptable.lock);

  proc->state = ZOMBIE;

  // Parent might be sleeping in wait().
  wakeup1(proc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == proc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.

  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;

  acquire(&ptable.lock);
  for(;;){
    proc->chan = (int)proc;
    proc->state = SLEEPING;
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;

        proc->chan = 0;
        proc->state = RUNNING;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      proc->chan = 0;
      proc->state = RUNNING;
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sched();
  }
}

void
freeproc(struct proc *p)
{
  if (!p || p->state != ZOMBIE)
    panic("freeproc not zombie");
  kfree(p->kstack);
  p->kstack = 0;
  freevm(p->pgdir);
  p->killed = 0;
  p->chan = 0;
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&cpu->scheduler, proc->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      proc = 0;
      if (p->state == ZOMBIE)
        freeproc(p);
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state.
void
sched(void)
{
  int intena;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(cpu->ncli != 1)
    panic("sched locks");
  if(proc->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = cpu->intena;
  swtch(&proc->context, cpu->scheduler);
  cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  proc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    initlog();
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  if(proc == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }

  // Go to sleep.
  proc->chan = (int)chan;
  proc->state = SLEEPING;


  sched();

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == (int)chan){
      // Tidy up.
      p->chan = 0;
      p->state = RUNNABLE;
    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}



int push(struct cstack *cstack, int sender_pid, int recepient_pid, int value) {
  struct proc *p;
  int ans = 1;
                                                                                            //  cprintf("push acuire\n");
  acquire(&ptable.lock);
  struct cstackframe *newSig;
  for(newSig = cstack->frames ;  newSig < cstack->frames + 10; newSig++){
    if (cas(&newSig->used, 0, 1))
      break;
  }
  if (newSig == cstack->frames + 10){//no free cell
    ans = 0;
    cprintf("\n\n             ERROR - NO free cell during proc.c::push !!!\n\n\n");
  }
  else{
    newSig->sender_pid = sender_pid;
    newSig->recepient_pid = recepient_pid;
    newSig->value = value;
    do {
      newSig->next = cstack->head;
    } while (!cas((int*)&cstack->head, (int)newSig->next, (int)newSig  ));

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->pid== recepient_pid){
          if(p->sigPauseInvoked){
            p->state = RUNNABLE;
            p->sigPauseInvoked=0;
          }
          break;//recipient_pid is valid
        }
    }
  }
                                                                                              //cprintf("     push release\n");
  release(&ptable.lock);
  return ans;
}

struct cstackframe *pop(struct cstack *cstack) {
                                                                                            //  cprintf("pop acuire\n");
  acquire(&ptable.lock);
  struct cstackframe *top;
  do {
    top = cstack->head;
    if (top==0)
      break;
  } while (!cas((int*)&cstack->head, (int)top, (int)top->next  ));
                                                                                              //  cprintf("     pop release\n");
  release(&ptable.lock);
  return top;
}





//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n\n\n");
  }

  // cprintf("HEY1\n");
  // struct cstack testMX;
  // struct cstack* testM = &testMX;
  //
  // struct cstackframe *newSig;
  // for(newSig = testM->frames ;  newSig < testM->frames + 10; newSig++){
  //   newSig->used = 0;
  // }
  // testM->head = 0;
  //
  //
  // cprintf("1st push return: %d!\n", push(testM, 9,9,9));
  // push(testM, 4,5,6);
  // push (testM, 1,2,3);
  // push(testM, 4,5,6);
  // push (testM, 1,2,3);
  // push(testM, 4,5,6);
  // push (testM, 1,2,3);
  // push(testM, 4,5,6);
  // push (testM, 1,2,3);
  // cprintf("10th push return: %d!\n", push(testM, 4,5,6));
  //
  // cprintf("11th push return: %d!\n", push(testM, 4,5,6));
  //
  // pop(testM)->used=0;
  // cprintf("extra pop return 1\n 12th push return: %d!\n", push(testM, 4,5,6));
  //
  // pop(testM)->used=0;
  //   pop(testM)->used=0;
  //     pop(testM)->used=0;
  //       pop(testM)->used=0;
  //         pop(testM)->used=0;
  //           pop(testM)->used=0;
  //             pop(testM)->used=0;
  //               pop(testM)->used=0;
  //     cprintf("9th pop returns: %d!\n", pop(testM)->value);
  //                   cprintf("10th pop returns: %d!\n", pop(testM)->value); //should release the used of the 2 last pops as well!!   !!
  //
  //                   cprintf("11th pop returns: %d!\n", (int)pop(testM));
  //
  //                 cprintf("13th push return: %d!\n", push(testM, 7,7,7));
  //                 cprintf("12th pop returns: %d!\n", (int)pop(testM)->value);
  //               //this test should return 1 1 0 1 1  6 9 0 1 7
  //
  //
  // cprintf("HEY2\n");
}



sig_handler sigset(sig_handler new_sig_handler) {
  sig_handler old_sig_handler = proc->sighandler;
  proc->sighandler = new_sig_handler;
  return old_sig_handler;
}

int sigsend(int dest_pid, int value) {
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if (p->pid == dest_pid)
      goto sigsend_dest_pid_found;
  }
  return -1; // dest_pid wan't found, meaning it's not a valid pid. return error
sigsend_dest_pid_found:
  if (push(&p->cstack, proc->pid, dest_pid, value) == 0)
    return -1; // pending signal stack is full. return error
  return 0; // successful execution
}

void sigret(void) {
  memmove(proc->tf, &proc->oldTf, sizeof(struct trapframe));
  proc->ignoreSignals = 0;
}

void sigpause(void) {
                                                                                            //  cprintf("sigPause acuire\n");
  acquire(&ptable.lock);
  if (proc->cstack.head==0){//only if the stack is EMPTY we want to go to sleep!!
    proc->state =SLEEPING;
    proc->sigPauseInvoked = 1;
    sched();  //scheded is called with the table locked
  }

                                                                                            //  cprintf("     sigpase realse\n");
  release(&ptable.lock);
}

void checkSignals(void){
  if (proc == 0)
    return;
  if (proc->ignoreSignals)
    return;
  struct cstackframe *poppedCstack = pop(&proc->cstack);
  if (poppedCstack == (struct cstackframe *)0)//empty stack
    return;
  if(proc->sighandler == (sig_handler)-1)//default handler does nothing
    return;
  proc->ignoreSignals = 1;
  memmove(&proc->oldTf, proc->tf, sizeof(struct trapframe));//backing up trap frame
  proc->tf->esp -= (uint)&invoke_sigret_end - (uint)&invoke_sigret_start;
  memmove((void*)proc->tf->esp, invoke_sigret_start, (uint)&invoke_sigret_end - (uint)&invoke_sigret_start);
  *((int*)(proc->tf->esp-4)) = poppedCstack->value;
  *((int*)(proc->tf->esp-8)) = poppedCstack->sender_pid;
  *((int*)(proc->tf->esp-12)) = proc->tf->esp;// return adress is a compiled code that calls sigret
  proc->tf->esp -= 12;
  proc->tf->eip = (uint)proc->sighandler;//setting the first instruction the be excuted after trapret

  poppedCstack->used=0;//realsing the cell;
}
