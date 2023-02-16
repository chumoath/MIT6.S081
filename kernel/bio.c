// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
/*
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
 // struct buf head;
} bcache;

*/

/*
  idea: the way to avoid race condition, but still use lock, but no need to wait

    every bucket maintain one double-linked list, rather than one double-linked, it is similar to kalloc

    that maintain one freelist each cpu

    init: add all buf to bucket[0]'s double-link

    this is a very clever idea, and can use widely in the design of lock/ concurrent

  multiprocessor
    spinlock's acquire can avoid current process is executed by other cpu
*/

/*
  bucket[0] link layout

  head
  refcnt == 0      5
  refcnt == 0      4
  refcnt == 0      3
  refcnt == 0      2
  refcnt == 0      1
  ...
  using
  ...
  unused

  principle
    be similar to kalloc, when init, alloc all buf to the first buckets

    other buckets steal buf from this bucket

    a lock for this bucket, protect the double link that this bucket maintain


    kalloc =>  kfree will return this page to current cpu's freelist, it is very clever
    b_get  =>  b_get will directly change this buf to current bucket
           =>  b_relse will place it to cur bucket's begin, it is very clever too 

  b_get
    1. find dev and blockno from cur bucket, find it, will return directly
    2. alloc one cache (buf)

  alloc cache, from end to start
    1. unused
    2. the least recently used, to replace
    3. no refcnt == 0, find other buckets' cached by the same way 
        delete from another bucket, must acquire the lock of this bucket firstly 

  release
    1. if ref_cnt == 0, set this buf to current bucket's begin 
*/


/*
  the sequence for lock
    prinple: must acquire when need
  
  compare:
    1. forever hold hash, util find a buf from another bucket 
      process1
          acquire hash
          acquire i     find
      
      process2
          acquire i
          acquire hash  it is impossible to find
      
      it is very possible to cause deadlock
    
    2. when no find from current bucket, release cur lock; find buf from other buckets, then acquire hash's lock
      process1
          acquire hash
          release hash

          acquire i        find buf from i
          acquire hash     find buf, acquire hash


      process2
          acquire i
          release i

          acquire hash      find buf from hash, it is impossible to find buf
          acquire i         it is impossilbe to acquire i
                            so it is impossilbe to cause deadlock
  summary:
    forever  must acquire one lock when it is necessary

    if no need to access the data struct, then release this lock right now
      it is a good way to avoid deadlock
*/


#define BUCKETS  13

struct buf buf[NBUF];
struct buf buckets[BUCKETS];  // head
struct spinlock locks[BUCKETS];

void
binit(void)
{
  /*
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  */

  struct buf * b;
  // only one process will execute, because no have other process
  for (int i = 0; i < BUCKETS; ++i) {
    // init lock
    initlock(&locks[i], "bcache");

    // init all buckets
    buckets[i].prev = &buckets[i];
    buckets[i].next = &buckets[i];
  }
  
  // init buckets, all insert to the first buckets
  for (b = buf; b < buf + NBUF; ++b) {
    b->next = buckets[0].next;
    b->prev = &buckets[0];
    buckets[0].next->prev = b;
    buckets[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  /*
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
  */

  struct buf * b;
  uint hash = blockno % BUCKETS;

  // find already cached
  acquire(&locks[hash]);
  for (b = buckets[hash].next; b != &buckets[hash]; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&locks[hash]);

      acquiresleep(&b->lock);
      return b;
    }
  }

  // no cached
  //  find from the last to begin, can use the unused firstly, then replace
  //  find from current bucket firstly
  for (b = buckets[hash].prev; b != &buckets[hash]; b = b->prev) {
    if (b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&locks[hash]);

      acquiresleep(&b->lock);
      return b;
    }
  }

  // avoid deadlock, only need, then acquire
  release(&locks[hash]);

  // no find in current buckets, find from other bucket
  // steal from other buckets, be similar to kalloc
  for (int i = 0; i  < BUCKETS; ++i) {
    // avoid deadlock
    if (i == hash) continue;

    acquire(&locks[i]);

    for (b = buckets[i].prev; b != &buckets[i]; b = b->prev) {

      if (b->refcnt == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        // buckets lock, protect the double link per bucket
        // buf.lock, protect the buf's data

        // move it to cur buckets
        b->next->prev = b->prev;
        b->prev->next = b->next;

        // acquire when needed
        acquire(&locks[hash]);

        b->next = buckets[hash].next;
        b->prev = &buckets[hash];

        buckets[hash].next->prev = b;
        buckets[hash].next = b;

        release(&locks[hash]);

        release(&locks[i]);

        acquiresleep(&b->lock);
        return b;
      }
    }  

    release(&locks[i]);
  }

  panic("no buf");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{

  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
/*
  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
  */
  uint hash = b->blockno % BUCKETS;
  acquire(&locks[hash]);

  b->refcnt--;

  // insert to cur buckets's head
  if (b->refcnt == 0) {
    // delete b
    b->next->prev = b->prev;
    b->prev->next = b->next;

    // set to head
    b->next = buckets[hash].next;
    b->prev = &buckets[hash];

    buckets[hash].next->prev = b;
    buckets[hash].next = b;
  }

  release(&locks[hash]);
}

void
bpin(struct buf *b) {
  uint hash = b->blockno % BUCKETS;

  acquire(&locks[hash]);
  b->refcnt++;
  release(&locks[hash]);
}

void
bunpin(struct buf *b) {

  uint hash = b->blockno % BUCKETS;
  acquire(&locks[hash]);
  b->refcnt--;
  release(&locks[hash]);
}


