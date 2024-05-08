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

// Bring ticks
extern uint ticks;

static void wakeup1(void *chan);

// Defined Hard Coded Array of Weights
int weights[40] ={
/*  0*/  88761, 71755, 56483, 46273, 36291,
/*  5*/  29154, 23254, 18705, 14949, 11916,
/* 10*/   9548,  7620,  6100,  4904,  3906,
/* 15*/   3121,  2501,  1991,  1586,  1277,
/* 20*/   1024,   820,   655,   526,   423,
/* 25*/    335,   272,   215,   172,   137,
/* 30*/    110,    87,    70,    56,    45,
/* 35*/     36,    29,    23,    18,    15,
};

/* Add Data structure for Priority Queue */
// struct node {
//   int data;
//   int priority;   // nice-value for this
// }

/* Defining Priority Queue for schedueling */

struct pqueue {
  struct proc *procs[NPROC]; 
  int size;                  
};

struct pqueue runnableQueue; 

// Initializing Heap
void initPQueue(struct pqueue *pq) {
  pq->size = 0;
}

// Inserting into Heap
void pushPQueue(struct pqueue *pq, struct proc *p) {
  if (pq->size == NPROC) {
    // error-handling..?
    return;
  }
  int i = pq->size++;
  while (i > 0 && pq->procs[(i-1)/2]->vruntime > p->vruntime) {
    pq->procs[i] = pq->procs[(i-1)/2];
    i = (i-1)/2;
  }
  pq->procs[i] = p;
}

// Pop out smallese vruntime
struct proc* popPQueue(struct pqueue *pq) {
  if (pq->size == 0) {
    return 0;
  }
  struct proc* min = pq->procs[0];
  struct proc* last = pq->procs[--pq->size];
  int i = 0;
  while (i*2 + 1 < pq->size) {
    int left = i*2 + 1, right = i*2 + 2;
    int j = left;
    if (right < pq->size && 
          pq->procs[right]->vruntime < pq->procs[left]->vruntime) {
      j = right;
    }
    if (pq->procs[j]->vruntime >= last->vruntime) break;
    pq->procs[i] = pq->procs[j];
    i = j;
  }
  pq->procs[i] = last;
  return min;
}

// Returnin min value
struct proc* peekPQueue(struct pqueue *pq) {
    if (pq->size == 0) {
        return 0; // Return NULL if the queue is empty
    }
    return pq->procs[0]; // Return the minimum element (the root of the heap)
}

/* Defining Priority Queue for schedueling */

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
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

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->nice = 20;             // Set default nice Value to 20.
  p->weight = weights[20];  // Set the default weight inorder to nice value 20.
  p->runtime = 0;           // Initializing runtime
  p->vruntime = 0;          // Initializing virtual runtime
  // p->ptick = 0;          // Initializing virtual runtime

  release(&ptable.lock);

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

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
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
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Copy process rutime,vruntime and nice value from proc.
  np->nice      = curproc->nice;
  np->runtime   = curproc->runtime;
  np->vruntime  = curproc->vruntime;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

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
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
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
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
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
  struct cpu *c = mycpu();
  c->proc = 0;
  


  for(;;){
    int tsum_runnable = 0;
    // Enable interrupts on this processor.
    sti();

    // int r_count=0;  // variable for counting whether scheduling is needed.

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    initPQueue(&runnableQueue);

    /* Changed for-loop for CFS */
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->state == RUNNABLE) {
        pushPQueue(&runnableQueue, p);
        tsum_runnable += weights[p->nice];
      }
    }
    p = popPQueue(&runnableQueue);
    if (p) {

      // debug code
      // cprintf("%d\n",p->pid);

      // p->ptick += 1000;
      // /* ADD RUNTIME INCREASE */
      // p->runtime += 1000;
      
      // // int tsum_runnable = findrunnable(&ticks);
      
      // if(tsum_runnable==0){
      //   p->timeslice = 10 * 1000 * 1; // Divide-by-Zero 발생?
      // } else{
      //   // timeslice 문제 (1000을 곱하냐 마냐)
      //   p->timeslice = 10 * 1000 * ((float)getweight(p->nice)/(float)tsum_runnable); // Divide-by-Zero 발생?
      // }

      // p->vruntime += 1000 * ((float)getweight(20) / (float)getweight(p->nice));

      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&(c->scheduler), p->context);
      switchkvm();
      c->proc = 0;
    }
    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  myproc()->ptick = 0;    // setting running tick to 0
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
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
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
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

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
  struct proc *sp;
  struct pqueue tempQueue;

  initPQueue(&tempQueue);

  for(sp = ptable.proc; sp < &ptable.proc[NPROC]; sp++) {
    if (sp->state == RUNNABLE) {
      pushPQueue(&tempQueue, sp);
    }
  }

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == SLEEPING && p->chan == chan) {
      sp = peekPQueue(&tempQueue);
      if(sp) {
        // p->runtime=0;
        p->ptick=0;
        p->vruntime = sp->vruntime - (1 * 1000 * (weights[20] / weights[p->nice]));
      } else {
        // p->runtime=0;
        p->ptick=0;
        p->vruntime = 0;
      }
      p->state = RUNNABLE;
    }
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
    cprintf("\n");
  }
}

int
getnice(int pid)
{
  struct proc *p;
  int p_nice;

  // copied skeleton from kill()
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid && pid>0){
      // Get nice value from PCB
      p_nice = p->nice;
      release(&ptable.lock);
      return p_nice;
    }
  }
  release(&ptable.lock);
  return -1;

}

int
setnice(int pid,int p_nice)
{
  struct proc *p;

  // copied skeleton from kill()
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid && pid>0){
      // Set nice value to PCB
      p->nice = p_nice;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;

}

void
ps(int pid)
{
  // copied skeleton from procdump()
  static char *states[] = {
  [UNUSED]    "UNUSED",
  [EMBRYO]    "EMBRYO",
  [SLEEPING]  "SLEEPING",
  [RUNNABLE]  "RUNNABLE",
  [RUNNING]   "RUNNING",
  [ZOMBIE]    "ZOMBIE"
  };
  // int i;
  struct proc *p;
  char *state;
  // uint pc[10];

  acquire(&ptable.lock);
  if(pid==0){
    //cprintf("name \t pid \t state \t priority \t runtime/weight \t runtime \t vruntime \t  \t tick %u\n",ticks);
    cprintf("name       pid     state     priority  runtime/weight  runtime  vruntime  tick %d\n",(int)ticks);
  }
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(pid != 0 && p->pid != pid){
      continue;
    } else if(pid != 0 && p->pid == pid){
      // cprintf("name \t pid \t state \t priority \t runtime/weight \t runtime \t vruntime\n");
      cprintf("name       pid     state     priority  runtime/weight  runtime  vruntime  tick %d\n",(int)ticks);
    }

    if(p->state >= 0 && p->state < NELEM(states) && states[p->state]) {
      state = states[p->state];
    } else {
      state = "???";
    }
    cprintf("%s \t %d \t %s \t %d \t \t %d \t %d \t %d\n",
         p->name, p->pid, state, p->nice,(int)(p->runtime/weights[p->nice]), p->runtime, p->vruntime);
  }
  release(&ptable.lock);

}

// calculate weight sum of runnable
int
calculatesum(void *chan)
{
  struct proc *p;
  int sum=0;
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == RUNNABLE /*&& p->chan == chan*/) // 여기 주석처리 했었음.
      sum += weights[p->nice];
  }
  
  return sum;
}

// find runnable
int
findrunnable(void *chan)
{
  int total_weight = 0;
  
  acquire(&ptable.lock);
  total_weight = calculatesum(chan);
  release(&ptable.lock);

  return total_weight;
}

// return weight from hard-code array
int getweight(int nice){
  return weights[nice];
}

