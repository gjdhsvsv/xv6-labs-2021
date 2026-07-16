struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  uint64 timestamp; // last time this buffer became unused
  struct buf *prev; // hash bucket list
  struct buf *next;
  uchar data[BSIZE];
};
