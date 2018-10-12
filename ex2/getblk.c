/* 擬似コード
 *
 * struct buf_header *getblk(int blkno) {
 *       while (buffer not found) {
 *          if (blkno in hash queue) {
 *              if (buffer locked) {
 *                  SENARIO-5
 *                  sleep(event buffer becomes free);
 *                  continue;
 *              }
 *              SENARIO-1
 *              make buffer locked;
 *              remove buffer from free list;
 *              return pointer to buffer;
 *          } else {
 *              if (not buffer on free list) {
 *                  SENARIO-4
 *                  sleep(event any buffer becomes free);
 *                  continue;
 *              }
 *              remove buffer from free list;
 *              if (buffer marked for delay write) {
 *                  SENARIO-3
 *                  asynchronous write buffer to disk;
 *                  ontinue;
 *              }
 *              SENARIO-2
 *              remove buffer from old hash queue;
 *              put buffer onto new hash queue;
 *              return pointer to buffer;
 *          }
 *      }
 *  }
 */

#include <stdio.h>
#include "blk.h"

struct buf_header *getblk(int blkno) {

}

struct buf_header* search_hash(int blkno) {
    int h;
    struct buf_header *p;
    h = hash(blkno);
    for (p = hash_head[h].hash_fp; p != &hash_head[h]; p = p->hash_fp) {
        if (p->blkno == blkno) {
        return p;
        }
    }
    return NULL;
}

void insert_head(struct buf_header *h, struct buf_header *p) {
    p->hash_bp = h;
    p->hash_fp = h->hash_fp;
    h->hash_fp->hash_bp = p;
    h->hash_fp = p;
}

void insert_tail(struct buf_header *h, struct buf_header *p) {
    p->hash_fp = h->hash_fp;
    p->hash_bp = h;
    h->hash_fp = p;
    p->hash_fp->hash_bp = p;
}

void remove_from_hash(struct buf_header *p) {
    p->hash_bp->hash_fp = p->hash_fp;
    p->hash_fp->hash_bp = p->hash_bp;
    p->hash_fp = NULL;
    p->hash_bp = NULL;
}
