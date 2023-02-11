#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  
  // next, if exception or interrupt occur, will enter kernelvec
  // user space:  need to change satp to kernel's page_table, store user's register,
  //                      set sp to kstack's sp 
  //       syscall       =>   ecall
  //       device intr   =>   
  //       exception     =>   /0

  // because the satp still point to user's page-table,
  //         so the uservec must be mapped to user space
  //         map trapoline to the last page of user virtual space


  // kernel space: no need to change satp, no need to set sp to kstack's sp
  //               only store kernel thread's register

  //       device intr
  //       exception       => always a error in xv6, panic


  // trap step
  // 1. if trap is a device interrupt and sstatus's SIE is clear, defer
  // 2. disable interrupt by clearing SIE
  // 3. copy pc to spec (sret will set spec to pc)
  // 4. save current mode (user / supervisor) in the SPP bit in sstatus
  // 5. set scause to reflect the trap's cause
  // 6. set mode to supervisor
  // 7. copy stvec to the pc



  // stvec    =>   one trap occurs, will set it to pc
  // spec     =>   one trap occurs, will set pc to it
  //          pair

  // sstatus  =>   SIE  interrupt enable/disable
  //          =>   SPP  which mode that trap came from, and sret will return
  // scause   =>   the reason for the trap




  // page-fault  type
  //  1. load, load data from a vp
  //  2. store, store date to a vp
  //  3. instruction, fetch instruction from a vp

  // scause   =>   the type of page fault
  // stval    =>   the address that couldn't be translated


  // combination of  page-table and page-fault
  // features: COW and lazy allocation and paging from disk (evict some page to disk), automatically extending stacks
  //           memory-mapped files

  // copy-on-write => only copy one page at stval for parent and child
  // share         => set read-only in parent and child's page-table's pte =>   heap, stack need to be shared, the data segment may also share
  


  // lazy allocation
  //       sbrk only alloc page table, and set pte is invalid
  //       when user use, use page-fault to actually allocate

  // paging from disk -> need more memory than available pysical ram
  //    evict one page to disk, and mark its pte to invalid  
  //    kernel can inspect the address, if it is the address whose pages is evict to disk
  //    evict on page, read this page from disk, mark its pte to valid

  // here
  //     a trap occurs, SIE is clear, and interrupt is disable
  //     so, the window of from uservec to intr_on(), will not reponse dev_intr, because the stvec still point to uservec
  // set stvec to kernelvec, and will intr_on after that
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
    if (which_dev == 2) {
      // interval and handler can both 0, because user's va can be zero
      // if interval is 0, the alarm_ddl is impossible >= ticks
      if (!p->alarm_flag && p->alarm_ddl >= ticks) {
        p->alarm_flag = 1;
        p->alarm_trapframe = *p->trapframe;
        p->alarm_ddl = ticks + p->alarm_interval;
        // after usertrapret, execute handler
        p->trapframe->epc = p->alarm_handler;
      }
    }
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  
  // SIE is the interrupt flag
  // should disabled interrupt here
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  // check and process dev interrupt
  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

