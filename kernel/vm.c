#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.  PROVIDE can provide a sign

extern char trampoline[]; // trampoline.S

extern int copyin_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len);
extern int copyinstr_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max);


// only pa is lianxu 
void
ukvmmap(pagetable_t p, uint64 va, uint64 pa, uint64 sz, int perm){
  if(mappages(p, va, sz, pa, perm) != 0)
    panic("ukvmmap");
}

/*
 * alloc new pagetable for user's kernel_pagetable
 * only copy the last pte, this is the data's page, not the page_table's page
*/
void
u_cpy_global_kernel_pagetable(pagetable_t * u_kernel_pagetable){
	*u_kernel_pagetable = (pagetable_t) kalloc(); // kernel have own page_table root
  	memset(*u_kernel_pagetable, 0, PGSIZE);

	// uart registers
	ukvmmap(*u_kernel_pagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);

	// virtio mmio disk interface
	ukvmmap(*u_kernel_pagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

	// CLINT
  // not map CLINT, bacause it is much low, 0x02000000, can't sbrk 100M
	// ukvmmap(*u_kernel_pagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);

	// PLIC
	ukvmmap(*u_kernel_pagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

	// map kernel text executable and read-only.
	ukvmmap(*u_kernel_pagetable, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

	// map kernel data and the physical RAM we'll make use of.
	ukvmmap(*u_kernel_pagetable, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

	// map the trampoline for trap entry/exit to
	// the highest virtual address in the kernel.
	ukvmmap(*u_kernel_pagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}


// before this, should free the kstack's page, and the rest of kernel's page shouldn't be changed
void
u_free_global_kernel_pagetable(pagetable_t p, uint64 u_sz){

    // uart registers
  	uvmunmap(p, UART0, 1, 0);

  	// virtio mmio disk interface
  	uvmunmap(p, VIRTIO0, 1, 0);

  	// CLINT
  	// uvmunmap(p, CLINT, 0x10, 0);

  	// PLIC
  	uvmunmap(p, PLIC, 0x400, 0);
	
	  uvmunmap(p, KERNBASE, (PHYSTOP - KERNBASE) >> 12, 0);

  	// unmap the trampoline for trap entry/exit to
  	// the highest virtual address in the kernel.

  	uvmunmap(p, TRAMPOLINE, 1, 0);

	// unmap user's kernel pagetable's user space map

	uvmunmap(p, 0, PGROUNDUP(u_sz)/PGSIZE, 0);

  	// freewalk must assure that every the last pte is invalid
  	freewalk(p);
}

/*
 * create a direct-map page table for the kernel.
 */

void
kvminit()
{
  kernel_pagetable = (pagetable_t) kalloc(); // kernel have own page_table root
  memset(kernel_pagetable, 0, PGSIZE);

  // uart registers
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];   // PX          L2 (9)   L1 (9)  L0 (9) PGSHIFT(12)    (va >> (PGSHIFT + level*9)) & 0x0FFF 
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);  // continue to extract next level pte
    } else {
      // current pte is not vaild
      //   not allow to alloc, or alloc a page equal to 0, return 0 to mean fail

      //   if alloc success, next loop traverse the pagetable
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
	  // all entry is valid
      memset(pagetable, 0, PGSIZE);
	  // set the entry to the alloced just and set to valid
      *pte = PA2PTE(pagetable) | PTE_V;                       // R W X is all 0, is middle pagetable
    }
  }
  // return the va's last level pte
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pte = walk(kernel_pagetable, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.


// only adapt to the pyhsical space is lianxu
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    // auto alloc the middle of  page_table
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.

// return pa, to be used by mapping to user_kernel_pagetable
char *
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  // map to user page_table
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
  return mem;
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;
  

  // text data guard page stack page  excess PLIC, fail
  // CLINT mapped to user's kernel pagetable
  // expand mem excess the CLINT, bacause CLINT also map to user's kernel pagetable, if it is mapped to user space again, it will cause remap

  if(newsz > PLIC)
	  return 0;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){ // RWX all 0, is a middle pagetable
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;                                    // clean this entry
    } else if(pte & PTE_V){                                // no free leaf, already free
	                                                       // it should be all 0, shouldn't valid
      panic("freewalk: leaf");
    }
  }
  // traverse all entry, free this page that pagetable is in 
  kfree((void*)pagetable);                                 // free pagetable, not page
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
	// only free the page
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);       // do_free is 1, free the last pte
  
  // free the pagetable and the middle pagetable
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  // user va from 0 start, so can loop from 0
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");

    // pa of the page
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    
    // copy old page to new page
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);

    // map the physical page to the identical va in new proc
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}


// copy map from user's page_table 's user's space to user's kernel pagetable's page_table
// from 0 to sz
void
uvmcopymap(pagetable_t kernel_pagetable, pagetable_t user_pagetable, uint64 sz){
	uint64 sz1 = PGROUNDUP(sz);
	pte_t * pte;
	uint64 pa;
	int flag;
	for(uint64 i = 0; i < sz1; i += PGSIZE){

		// find pa
		if((pte = walk(user_pagetable, i, 0)) == 0)
			panic("uvmcopymap");

		if((*pte & PTE_V) == 0)
			panic("not mapped copy");
		
		// can't identify PTE_U, bacause the guard page is not PTE_U
		// if((*pte & PTE_U) == 0)
		// 	panic("not user page");

		if(PTE_FLAGS(*pte) == PTE_V)
			panic("not a leaf");

		// map
		pa = PTE2PA(*pte);
		flag = PTE_FLAGS(*pte);
		// kernel mode cann't access the page that have pte_u 
		ukvmmap(kernel_pagetable, i, pa, PGSIZE, flag & ~PTE_U);
	}
}


void ukvmaddmap(pagetable_t kernel_pagetable, pagetable_t user_pagetable, uint64 oldsz, uint64 newsz){
  oldsz = PGROUNDUP(oldsz);
  newsz = PGROUNDUP(newsz);
	pte_t * pte;
	uint64 pa;
	int flag;
	for(uint64 i = oldsz; i < newsz; i += PGSIZE){

		// find pa
		if((pte = walk(user_pagetable, i, 0)) == 0)
			panic("uvmcopymap");

		if((*pte & PTE_V) == 0)
			panic("not mapped add");
		
		// can't identify PTE_U, bacause the guard page is not PTE_U
		// if((*pte & PTE_U) == 0)
		// 	panic("not user page");

		if(PTE_FLAGS(*pte) == PTE_V)
			panic("not a leaf");

		// map
		pa = PTE2PA(*pte);
		flag = PTE_FLAGS(*pte);
		// kernel mode cann't access the page that have pte_u 
		ukvmmap(kernel_pagetable, i, pa, PGSIZE, flag & ~PTE_U);
	}
}

void ukvmunmap(pagetable_t kernel_pagetable, uint64 oldsz, uint64 newsz){
  uint64 npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
  uvmunmap(kernel_pagetable, PGROUNDUP(newsz), npages, 0); // only unmap from user's kernel page table
}



// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
	return copyin_new(pagetable, dst, srcva, len);
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
	return copyinstr_new(pagetable, dst, srcva, max);
}

static void
printpg(int level, pte_t * pg_pa){
	for(uint64 i = 0; i < PGSIZE / sizeof(pte_t); ++i){
		pte_t data = pg_pa[i];
		uint64 pa = (data >> 10) << 12;

		// valid
		if((data & 1UL) != 0){
			switch (level)
			{
			case 1:
				printf("..");
				break;
			case 2:
				printf(".. ..");
				break;
			case 3:
				printf(".. .. ..");
				break;
			}

			printf("%d: pte %p pa %p\n", i, (void *)data, (void*)pa);

			// next level
			if(level != 3)
				printpg(level+1, (pte_t*)pa);
		}
	}
}


// no need the satp to translate, the kernel can use paddr directly
// exec is executed on kernel
void
vmprint(pagetable_t pagetable){
	printf("page table %p\n", pagetable);
	printpg(1, (pte_t*)pagetable);
}