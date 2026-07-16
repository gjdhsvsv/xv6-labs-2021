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

#define NBUCKET 13

struct bucket {
  struct spinlock lock;
  struct buf head;
};

struct {
  struct spinlock eviction_lock;
  struct bucket bucket[NBUCKET];
  struct buf buf[NBUF];
  uint64 clock;
} bcache;

static uint
bhash(uint dev, uint blockno)
{
  return (dev + blockno) % NBUCKET;
}

// The caller must hold the bucket lock.
static struct buf*
blookup(uint bucket, uint dev, uint blockno)
{
  struct buf *b;

  for(b = bcache.bucket[bucket].head.next;
      b != &bcache.bucket[bucket].head; b = b->next)
    if(b->dev == dev && b->blockno == blockno)
      return b;
  return 0;
}

// The caller must hold the lock for b's bucket.
static void
bremove(struct buf *b)
{
  b->prev->next = b->next;
  b->next->prev = b->prev;
}

// The caller must hold the bucket lock.
static void
binsert(uint bucket, struct buf *b)
{
  struct buf *head = &bcache.bucket[bucket].head;

  b->next = head->next;
  b->prev = head;
  head->next->prev = b;
  head->next = b;
}

static void
lockbuckets(uint first, uint second)
{
  if(first == second){
    acquire(&bcache.bucket[first].lock);
  } else if(first < second){
    acquire(&bcache.bucket[first].lock);
    acquire(&bcache.bucket[second].lock);
  } else {
    acquire(&bcache.bucket[second].lock);
    acquire(&bcache.bucket[first].lock);
  }
}

static void
unlockbuckets(uint first, uint second)
{
  if(first == second){
    release(&bcache.bucket[first].lock);
  } else if(first < second){
    release(&bcache.bucket[second].lock);
    release(&bcache.bucket[first].lock);
  } else {
    release(&bcache.bucket[first].lock);
    release(&bcache.bucket[second].lock);
  }
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.eviction_lock, "bcache.eviction");
  for(int i = 0; i < NBUCKET; i++){
    initlock(&bcache.bucket[i].lock, "bcache.bucket");
    bcache.bucket[i].head.prev = &bcache.bucket[i].head;
    bcache.bucket[i].head.next = &bcache.bucket[i].head;
  }

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->dev = (uint)-1;
    b->blockno = b - bcache.buf;
    b->timestamp = 0;
    binsert(bhash(b->dev, b->blockno), b);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b, *victim;
  uint bucket = bhash(dev, blockno);
  uint oldbucket;
  uint64 oldest, victim_timestamp;

  // Is the block already cached?
  acquire(&bcache.bucket[bucket].lock);
  if((b = blookup(bucket, dev, blockno)) != 0){
    b->refcnt++;
    release(&bcache.bucket[bucket].lock);
    acquiresleep(&b->lock);
    return b;
  }
  release(&bcache.bucket[bucket].lock);

  // Serialize cache misses, and check again after acquiring the eviction
  // lock so that concurrent misses cannot create duplicate buffers.
  acquire(&bcache.eviction_lock);
  acquire(&bcache.bucket[bucket].lock);
  if((b = blookup(bucket, dev, blockno)) != 0){
    b->refcnt++;
    release(&bcache.bucket[bucket].lock);
    release(&bcache.eviction_lock);
    acquiresleep(&b->lock);
    return b;
  }
  release(&bcache.bucket[bucket].lock);

  // Find the least recently used unreferenced buffer.  Bucket locks are
  // taken one at a time during this scan.
retry:
  victim = 0;
  oldest = ~(uint64)0;
  victim_timestamp = 0;
  for(int i = 0; i < NBUCKET; i++){
    acquire(&bcache.bucket[i].lock);
    for(b = bcache.bucket[i].head.next;
        b != &bcache.bucket[i].head; b = b->next){
      if(b->refcnt == 0 && b->timestamp <= oldest){
        victim = b;
        oldest = b->timestamp;
        victim_timestamp = b->timestamp;
      }
    }
    release(&bcache.bucket[i].lock);
  }
  if(victim == 0){
    release(&bcache.eviction_lock);
    panic("bget: no buffers");
  }

  oldbucket = bhash(victim->dev, victim->blockno);
  lockbuckets(oldbucket, bucket);

  // Recheck both the requested block and the victim while holding the
  // relevant locks.  A hit may have claimed or recently used the victim
  // while the LRU scan moved through other buckets.
  if((b = blookup(bucket, dev, blockno)) != 0){
    b->refcnt++;
    unlockbuckets(oldbucket, bucket);
    release(&bcache.eviction_lock);
    acquiresleep(&b->lock);
    return b;
  }
  if(victim->refcnt != 0 || victim->timestamp != victim_timestamp){
    unlockbuckets(oldbucket, bucket);
    goto retry;
  }

  if(oldbucket != bucket){
    bremove(victim);
    binsert(bucket, victim);
  }
  victim->dev = dev;
  victim->blockno = blockno;
  victim->valid = 0;
  victim->refcnt = 1;
  unlockbuckets(oldbucket, bucket);
  release(&bcache.eviction_lock);
  acquiresleep(&victim->lock);
  return victim;
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
void
brelse(struct buf *b)
{
  uint bucket;

  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  bucket = bhash(b->dev, b->blockno);
  acquire(&bcache.bucket[bucket].lock);
  if(b->refcnt < 1)
    panic("brelse: refcnt");
  b->refcnt--;
  if(b->refcnt == 0)
    b->timestamp = __sync_add_and_fetch(&bcache.clock, 1);
  release(&bcache.bucket[bucket].lock);
}

void
bpin(struct buf *b) {
  uint bucket = bhash(b->dev, b->blockno);

  acquire(&bcache.bucket[bucket].lock);
  b->refcnt++;
  release(&bcache.bucket[bucket].lock);
}

void
bunpin(struct buf *b) {
  uint bucket = bhash(b->dev, b->blockno);

  acquire(&bcache.bucket[bucket].lock);
  if(b->refcnt < 1)
    panic("bunpin: refcnt");
  b->refcnt--;
  if(b->refcnt == 0)
    b->timestamp = __sync_add_and_fetch(&bcache.clock, 1);
  release(&bcache.bucket[bucket].lock);
}

