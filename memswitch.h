#ifndef _MEMSWITCH_H
#define _MEMSWITCH_H
#include "hashtable.h"
#include <stdbool.h>
#define MAX_QUEUE_LENGTH
#define LOCK_LIST 0x00000001
#define LOCK_ITEM 0x00000002
#define LOCK_PAIR 0x00000004
#define LOCK_HEAP 0x00000008
#define MAX_INT 0x3f3f3f3f
#define _USE_LRU
/* #define CACHE_INSPECT */
/* #define _USE_LFU
 * #define _USE_MQ
 * #define _USE_ARC */

/* #define TEST */

#define MEM_MAX 4096*1024*20    //80MB cache

#ifdef _USE_LRU

typedef struct CacheList {
    struct HashTable* table;
    struct CacheHeap* cacheheap;
    struct CacheItem* front;
    struct CacheItem* rear;
    /* volatile int  lock; */
    volatile int del_lock;
    volatile int mem_free;
    pthread_mutex_t mutex;
    long long mem_max;
    long long num;
} CacheList;

typedef struct CacheItem {
    Content* cont;
    volatile int ref;
    /* volatile int lock; */
    pthread_mutex_t mutex;
    volatile int del_lock;      //delete lock
    struct CacheItem* next;
    struct CacheItem* pre;
    struct HeapItem* heapitem;
} CacheItem;

/* https://file.scirp.org/pdf/JILSA_2015111311031103.pdf */
/* Least -Frequently- Used (LFU) is another common Web
 *   caching policy that replaces the object with the least
 * number of accesses. LFU keeps popular Web
 *   objects and evicts rarely used ones. However, LFU suffers from
 * the cache pollution i n objects with the large reference accounts,
 *   which are never replaced even if these objects are not re- accessed again
 *  [1] [12] [13] . As attempt to reduce cache pollution caused by LFU, Least
 * -Frequently- Used -Dynamic -Aging (LFU -DA) is introduced by Arlitt
 * et al.  [14] . LFU -DA added dynamic aging to LFU pol- icy. LFU
 * -DA computes the key value K ( g) of object g using Equation (1).  () ( )
 * Kg   L Fg =  + (1) where F ( g) is the frequency of the visits of object g
 * ,  while L is a dynamic aging factor.  L is initialized to zero,
 * and then updated to the key value of the last removed object. */


/* * Every cache hit increases item `HIT` counter.
 * * Every cache miss increases `THRESHOLD`, until max `HITS` is reached.
 * * When full, a new cache item will only be accepted if `THRESHOLD` is
 *   above or equals the less frequently used item's HIT counter. Said
 *   item is evicted.
 * * When a new item is cached, its `HIT` counter is set equal to `THRESHOLD`
 *   itself.
 * * When an existing item is updated, its `HIT` counter is incremented
 *   by 1 above `THRESHOLD`.
 * * When any item's `HIT` reaches `MAX_HITS`, said value is substracted
 *   from every `HIT` and `THRESHOLD` counter. */

typedef struct HeapItem{
    volatile int f;
    volatile double k;
    volatile double l;
    CacheItem* cacheitem;
    int idx;
}HeapItem;

typedef struct CacheHeap{
    volatile double threashold;
    int max_hit;
    double c;
    HeapItem** array;
    unsigned int heap_size;
    /* pthread_mutex_t lock; */
    CacheList* cachelist;
    int (*heap_cmp)(void* a, void* b);
    void (*heap_swap)(void** array,int a,int b);
}CacheHeap;

CacheItem* AddCacheItem(CacheList* cachelist, Content** cont, char* file_name,
                         int lock_op);
int DelCacheItem(CacheList* cachelist, CacheItem** delitems, int del_num,
                 int lock_op);
int CacheListInit(CacheList** cachelist, int max_mem, HashTable* table);
int CacheListDestroy(CacheList* cachelist);
int LruReplace(CacheList* cachelist, Content* new_cont);
int LfuReplace(CacheList* cachelist, Content* new_cont);
int LfuRefer(CacheList* cachelist, CacheItem* cacheitem, int lock_op);
int ReadCacheItem(CacheList* cachelist, CacheItem* cacheitem, char* buffer);
int MoveCacheItemToTail(CacheList* cachelist, CacheItem* cacheitem,
                        int lock_op);
int HeapCmp(void* a, void* b);
void HeapSwap(void** array, int a,int b);
void CutDownThreashold(CacheHeap* cacheheap);
void CalculateK(HeapItem* heapitem, CacheHeap* cacheheap);

#endif

#endif
