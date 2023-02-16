// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

/*
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;
*/

struct kmem {
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmems[NCPU];

void
kinit()
{
  //initlock(&kmem.lock, "kmem");
  
  //char * pstart = (char *)PGROUNDUP((uint64)end);
  //char * pend = (char *)PGROUNDDOWN(PHYSTOP);
  //int pages = (pend - pstart) / PGSIZE;
  //int perpages = pages / NCPU;

  //for (int i = 0; i < NCPU; ++i) {
  //  initlock(&kmems[i].lock, "kmem");
  //  freerange(pstart + perpages * i * PGSIZE, pstart + perpages * (i + 1) * PGSIZE);
  //}
  //freerange(end, (void*)PHYSTOP);

  for (int i = 0; i < NCPU; ++i)
    initlock(&kmems[i].lock, "kmem");
  
  // kinit will only be invoke by cpu 0
  freerange(end, (void *)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // use cpuid must disable interrupt
  push_off();

  int cpu = cpuid();
  acquire(&kmems[cpu].lock);
  r->next = kmems[cpu].freelist;
  kmems[cpu].freelist = r;
  release(&kmems[cpu].lock);

  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  // request itself
  // disable interrupt, only run at this cpu
  push_off();
  int cpu = cpuid();
  // if switch cpu here, one cpu will use another cpu's freelist
  acquire(&kmems[cpu].lock);

  r = kmems[cpu].freelist;
  if(r)
    kmems[cpu].freelist = r->next;
  
  release(&kmems[cpu].lock);
  pop_off();

  // r is invalid, request others'
  if (!r) {
    int i;
    for (i = 0; i < NCPU; ++i) {
      acquire(&kmems[i].lock);
      // find memory
      if(kmems[i].freelist) {
        r = kmems[i].freelist;
        kmems[i].freelist = r->next;
        release(&kmems[i].lock);
        break;
      }
      release(&kmems[i].lock);
    }
  }
  // no need to set r to 0, because all is 0, will return 0

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  return (void*)r;
}
