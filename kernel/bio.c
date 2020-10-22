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

#define HASHSIZE 13

struct {
  struct spinlock headlock[HASHSIZE];
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
		b->timestamp = ticks;
		if(++st == HASHSIZE) st = 0;
  }
}

int hash(uint dev, uint blockno) { return (1234 * dev + 5678 * blockno + 90) % HASHSIZE; }


// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
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
	release(&bcache.headlock[id]);
	int choice = -1, tms = 0x3f3f3f3f;
	struct buf *ptr = 0;
	for(int i = 0; i < HASHSIZE; ++i) {
		acquire(&bcache.headlock[i]);
		b = bcache.freelist[i].prev;
		if(b->refcnt) panic("bget: freelist is not free!");
		if(b != &bcache.freelist[i] && b->timestamp<tms)
			ptr = b, choice = i, tms = b->timestamp;
	}
	if(choice == -1)
			panic("bget: no buffers");
	b = ptr;
	del(b), ins(b, &bcache.heads[id]);
	b->dev = dev;
	b->blockno = blockno;
	b->valid = 0;
	b->refcnt = 1;
	b->timestamp = ticks;
	for(int i = 0; i < HASHSIZE; ++i)
		release(&bcache.headlock[i]);
 	acquiresleep(&b->lock);
	return b;
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
  int id = hash(b->dev, b->blockno);
  acquire(&bcache.headlock[id]);
  b->refcnt++;
  release(&bcache.headlock[id]);
}

void
bunpin(struct buf *b) {
  int id = hash(b->dev, b->blockno);
  acquire(&bcache.headlock[id]);
  b->refcnt--;
  release(&bcache.headlock[id]);
}

