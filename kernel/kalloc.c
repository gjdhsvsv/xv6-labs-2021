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

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int count[(PHYSTOP - KERNBASE) / PGSIZE];
} krefs;

static int
refindex(uint64 pa)
{
  if(pa < KERNBASE || pa >= PHYSTOP || (pa % PGSIZE) != 0)
    panic("refindex");
  return (pa - KERNBASE) / PGSIZE;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&krefs.lock, "krefs");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    acquire(&krefs.lock);
    krefs.count[refindex((uint64)p)] = 1;
    release(&krefs.lock);
    kfree(p);
  }
}

void
krefinc(uint64 pa)
{
  int index = refindex(pa);

  acquire(&krefs.lock);
  if(krefs.count[index] < 1)
    panic("krefinc");
  krefs.count[index]++;
  release(&krefs.lock);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int index;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  index = refindex((uint64)pa);
  acquire(&krefs.lock);
  if(krefs.count[index] < 1)
    panic("kfree ref");
  krefs.count[index]--;
  if(krefs.count[index] > 0){
    release(&krefs.lock);
    return;
  }
  release(&krefs.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    int index = refindex((uint64)r);

    acquire(&krefs.lock);
    if(krefs.count[index] != 0)
      panic("kalloc ref");
    krefs.count[index] = 1;
    release(&krefs.lock);
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  return (void*)r;
}
