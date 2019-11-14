#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

#define MAX_FRAMES 16384

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
// defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

struct Frame {
  int pid;
  struct run *frame;
};

struct Frame Frames[MAX_FRAMES];

void kfree2(char *v) {
  struct run *r;

  if ((uint) v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  memset(v, 1, PGSIZE);

  if (kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run *) v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if (kmem.use_lock)
    release(&kmem.lock);
}

static void
setPID(struct run *frame, int pid) {
  for (int i = 0; i < MAX_FRAMES; i++) {
    if (Frames[i].frame == frame) {
      Frames[i].pid = pid;
      break;
    }
  }
}

static struct run *
getFree(int pid) {
  if (Frames[0].pid == 0 && (Frames[1].pid == 0 || Frames[1].pid == pid)) {
    Frames[0].pid = pid;
    return Frames[0].frame;
  }
  for (int i = 1; i < MAX_FRAMES; i++) {
    if (pid == -2 && !Frames[i].pid) {
      Frames[i].pid = pid;
      return Frames[i].frame;
    }
    else if ((Frames[i-1].pid == pid||Frames[i-1].pid ==0||Frames[i-1].pid==-2)
      && (Frames[i+1].pid == pid||Frames[i+1].pid == 0||Frames[i+1].pid==-2) && !Frames[i].pid) {
      Frames[i].pid = pid;
      return Frames[i].frame;
    }
  }
  return 0;
}

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void kinit1(void *vstart, void *vend) {
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void kinit2(void *vstart, void *vend) {
  freerange(vstart, vend);
  struct run *r = kmem.freelist;
  for (int i = 0; i < MAX_FRAMES; i++) {
    Frames[i].frame = r;
    Frames[i].pid = 0;
    r = r->next;
  }
  kmem.use_lock = 1;
}

void freerange(void *vstart, void *vend) {
  char *p;
  p = (char *) PGROUNDUP((uint) vstart);
  for (; p + PGSIZE <= (char *) vend; p += PGSIZE) {
    kfree2(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(char *v) {

  if ((uint) v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);
  if (kmem.use_lock)
    acquire(&kmem.lock);
  setPID((struct run *) v, 0);
  if (kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char *
kalloc(void) {
  struct run *r;

  if (kmem.use_lock)
    acquire(&kmem.lock);
  if (!kmem.use_lock){
    r = kmem.freelist;
    if (r) {
      kmem.freelist = r->next;
    }
  }
  else 
    r = getFree(-2);
  if (kmem.use_lock)
    release(&kmem.lock);
  return (char *) r;
}

char *
kalloc2(int pid) {
  struct run *r;

  if (kmem.use_lock)
    acquire(&kmem.lock);
  if (!kmem.use_lock){
    r = kmem.freelist;
    if (r) {
      kmem.freelist = r->next;
    }
  }
  else 
    r = getFree(pid);
  if (kmem.use_lock)
    release(&kmem.lock);
  return (char *) r;
}

int dump_physmem(int *frames, int *pids, int numframes) {
  if(!frames)
    return -1;
  if(!pids)
    return -1;
  if(!numframes)
    return -1;
  if (kmem.use_lock)
    acquire(&kmem.lock);
  int j = 0;
  for (int i = 0; i < MAX_FRAMES; i++) {
    if (Frames[i].pid != 0) {
      frames[j] = (V2P(Frames[i].frame) >> 12) & 0xffff;
        pids[j] = Frames[i].pid;
        ++j;
      }
      if (j == numframes) 
        break;
  }
  if (kmem.use_lock)
    release(&kmem.lock);
  return 0;
}
