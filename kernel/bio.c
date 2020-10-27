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

#define HASHSIZE 29

struct {
  struct spinlock headlock[HASHSIZE], evictlock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf heads[HASHSIZE], freelist[HASHSIZE];
} bcache;

void del(struct buf *ptr) { ptr->next->prev = ptr->prev, ptr->prev->next = ptr->next; }
void ins(struct buf *ptr, struct buf *pos) { ptr->next = pos->next, ptr->prev = pos, pos->next = pos->next->prev = ptr; }

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.evictlock, "bcache.evictlock");

  // Create linked list of buffers
  for(int i = 0; i < HASHSIZE; ++i) {
    initlock(&bcache.headlock[i], "bcache.headlock");
    bcache.heads[i].prev = &bcache.heads[i];
    bcache.heads[i].next = &bcache.heads[i];
    bcache.freelist[i].next = bcache.freelist[i].prev = &bcache.freelist[i];
  }
  int st = 0;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    ins(b, &bcache.freelist[st]);
    b->refcnt = 0;
    b->timestamp = ticks;
    if(++st == HASHSIZE) st = 0;
  }
}

int hash(uint dev, uint blockno) { return (1234 * dev + 5678 * blockno + 90) % HASHSIZE; }


// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
_bget(uint dev, uint blockno)
{
  struct buf *b;

  uint id = hash(dev, blockno);
  acquire(&bcache.headlock[id]);

  // Is the block already cached?
  for(b = bcache.heads[id].next; b != &bcache.heads[id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      b->timestamp = ticks;
      release(&bcache.headlock[id]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // Is the block cached in freelist?
  for(b = bcache.freelist[id].next; b != &bcache.freelist[id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      del(b), ins(b, &bcache.heads[id]);
      b->refcnt++;
      b->timestamp = ticks;
      release(&bcache.headlock[id]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  b = bcache.freelist[id].prev;
  if(b != &bcache.freelist[id] && b->refcnt == 0) {
    del(b), ins(b, &bcache.heads[id]);
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    b->timestamp = ticks;
    release(&bcache.headlock[id]);
    acquiresleep(&b->lock);
    return b;
  } else {
    push_off();
#ifdef LAB_LOCK
    __sync_fetch_and_add(&(bcache.evictlock.n), 1);
#endif
    while(__sync_lock_test_and_set(&bcache.evictlock.locked, 1) != 0) {
      __sync_synchronize();
      release(&bcache.headlock[id]);
      pop_off();
      return 0;
    }
    __sync_synchronize();
    bcache.evictlock.cpu = mycpu();
    for(int i = 0; i < HASHSIZE; ++i) if(i != id) {
      acquire(&bcache.headlock[i]);
      b = bcache.freelist[i].prev;
      if(b != &bcache.freelist[i] && b->refcnt == 0) {
        del(b), ins(b, &bcache.heads[id]);
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        b->timestamp = ticks;
        release(&bcache.evictlock);
        release(&bcache.headlock[id]);
        for(int j = 0; j <= i; ++j) if(j != id)
          release(&bcache.headlock[j]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    panic("No more free block!");
  }
  panic("Control reaches end of _bget");
}

static struct buf *bget(int dev, int blockno) {
  struct buf *ret;
  while((ret = _bget(dev, blockno)) == 0);
  return ret;
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

  int id = hash(b->dev, b->blockno);
  acquire(&bcache.headlock[id]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    del(b), ins(b, &bcache.freelist[id]);
  }
  
  release(&bcache.headlock[id]);
}
void
bpin(struct buf *b) {
  if(!holdingsleep(&b->lock))
    panic("brelse");
  int id = hash(b->dev, b->blockno);
  acquire(&bcache.headlock[id]);
  b->refcnt++;
  release(&bcache.headlock[id]);
}

void
bunpin(struct buf *b) {
  if(!holdingsleep(&b->lock))
    panic("brelse");
  int id = hash(b->dev, b->blockno);
  acquire(&bcache.headlock[id]);
  b->refcnt--;
  release(&bcache.headlock[id]);
}

