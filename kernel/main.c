#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // physical page allocator
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode cache
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
    userinit();      // first user process     the paging haven't been opened, so can setting pagetable of process, but cann't use the vmaddr(only can set pagetable)
                                               // only set the first user process at physical addr, haven't set the paging
                                               // the forkret set the userret, it is the vaddr, and the process will run when paging open
                                               // so, beakpoint should break at the lastest page of the vaddr
                                               // the hardware supports 39bit, the xv6 use 38bit to avoid sign-extension when 39th bit is signed, the mmu extent it to 64bit, and the hardware truncate it to 39bit
                                               //           to performance
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();        
}
