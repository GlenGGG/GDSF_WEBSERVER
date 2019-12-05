#include "memswitch.h"
#include "hashtable.h"
#include "min_heap.h"
#include "thread_pool.h"
#include "webserver.h"
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void CalculateK(HeapItem* heapitem, CacheHeap* cacheheap)
{
    if (heapitem->cacheitem->cont->length <= 0) {
        Error("Calculate 20 length<=0", "webserver.log");
    }
    heapitem->k = (double)(heapitem->f)
            * ((double)(cacheheap->c)
                  / (double)(heapitem->cacheitem->cont->length))
        + heapitem->l;
}
void CutDownThreashold(CacheHeap* cacheheap)
{
    for (int i = 0; i < cacheheap->heap_size; ++i) {
        (cacheheap->array[i]->f) >>= 1;
        (cacheheap->array[i]->l) /= 2;
    }
    cacheheap->threashold /= 2;
}
int CacheListInit(CacheList** cachelist, int max_mem, HashTable* table)
{
    *cachelist = malloc(sizeof(struct CacheList));
    if (cachelist == NULL)
        return -1;
    (*(cachelist))->front = NULL;
    (*(cachelist))->rear = (*(cachelist))->front;
    (*(cachelist))->mem_max = max_mem;
    (*(cachelist))->mem_free = (*(cachelist))->mem_max;
    /* (*(cachelist))->lock = 0; */
    pthread_mutex_init(&((*(cachelist))->mutex), NULL);
    (*(cachelist))->del_lock = 0;
    (*(cachelist))->table = table;
    (*(cachelist))->num = 0;
    (*(cachelist))->cacheheap = malloc(sizeof(CacheHeap));
    (*(cachelist))->cacheheap->array
        = calloc(MAX_FILE_LINE_NUM + 5, sizeof(HeapItem*));
    (*(cachelist))->cacheheap->heap_cmp = HeapCmp;
    (*(cachelist))->cacheheap->cachelist = *cachelist;
    (*(cachelist))->cacheheap->max_hit = MAX_INT;
    (*(cachelist))->cacheheap->threashold = 0;
    (*(cachelist))->cacheheap->heap_size = 0;
    (*(cachelist))->cacheheap->c = 1;
    (*(cachelist))->cacheheap->heap_swap = HeapSwap;

    return 0;
}

inline void HeapSwap(void** array, int a, int b)
{
    assert(a >= 0);
    assert(b >= 0);
    assert(array != NULL);
    assert(((HeapItem*)array[a])->idx == a);
    assert(((HeapItem*)array[b])->idx == b);
    void* t = array[a];
    array[a] = array[b];
    array[b] = t;
    ((HeapItem*)array[a])->idx = a;
    ((HeapItem*)array[b])->idx = b;
    /* if (a < b) {
     *     assert(2*a+1==b||2*a+2==b);
     *     assert(((HeapItem*)array[a])->f < ((HeapItem*)array[a])->f);
     * } else {
     *     assert(2*b+1==a||2*b+2==a);
     *     assert(((HeapItem*)array[a])->f >= ((HeapItem*)array[a])->f);
     * } */
}
inline int HeapCmp(void* a, void* b)
{
    assert(a != NULL);
    assert(b != NULL);
    HeapItem* ca = (HeapItem*)a;
    HeapItem* cb = (HeapItem*)b;
    if (ca->k < cb->k)
        return -1;
    else if (ca->k == cb->k)
        return 0;
    else
        return 1;
}

/* lock_op can be LOCK_LIST or 0 */
CacheItem* AddCacheItem(
    CacheList* cachelist, Content** cont, char* file_name, int lock_op)
{
#ifndef TEST
    if (file_name == NULL || cachelist == NULL) {
        Error("file_name == NULL||cachelist == NULL", "webserver.log");
        return NULL;
    }
#else
    if (cachelist == NULL) {
        Error("cachelist == NULL", "webserver.log");
        return NULL;
    }
#endif
    else {
        Content* cont_tmp;
        if (cont != NULL)
            cont_tmp = *cont;
        else
            cont_tmp = malloc(sizeof(Content));
        if (cont_tmp == NULL) {
            Error("cont_tmp malloc\n", "webserver.log");
            return NULL;
        }
        cont_tmp->address = NULL;
        cont_tmp->pair = NULL;
        cont_tmp->cacheitem = NULL;
        cont_tmp->file_name = NULL;
        cont_tmp->is_ready = false;
        pthread_mutex_init(&(cont_tmp->mutex), NULL);
        pthread_cond_init(&(cont_tmp->cond), NULL);
#ifndef TEST
        cont_tmp->length = GetFileSize(file_name);
#else
        cont_tmp->length = rand() % 2048;
#endif
        /* lock list */
        if (cont_tmp->length < 0) {
            free(cont_tmp);
            return NULL;
        }
        if (cachelist->del_lock) {
            Error("cachelist destory", "webserver.log");
            free(cont_tmp);
            return NULL;
        }
        if (lock_op & LOCK_LIST)
            pthread_mutex_lock(&(cachelist->mutex));
        HashPair* pair_check = NULL;
        if (((pair_check = FindHashByKeyNoAdd(cachelist->table, file_name))
                != NULL)) {
            if (pair_check->cont->length != GetFileSize(file_name)) {
                Error("AddCacheItem 263 length", "webserver.log");
            }
            if (pair_check->cont->cacheitem == NULL
                || pair_check->cont != pair_check->cont->cacheitem->cont) {
                Error("AddCacheItem 126 pair_check", "webserver.log");
            }
            free(cont_tmp);
            if (lock_op & LOCK_LIST)
                pthread_mutex_unlock(&(cachelist->mutex));
            return pair_check->cont->cacheitem;
        }
        struct CacheItem* cacheitem;
        cacheitem = malloc(sizeof(CacheItem));
        if (cacheitem == NULL) {
            if ((lock_op & LOCK_LIST)) {
                pthread_mutex_unlock(&(cachelist->mutex));
            }
            Error("cacheitem malloc", "webserver.log");
            free(cont_tmp);
            return NULL;
        }
        cacheitem->cont = cont_tmp;
        cacheitem->cont->cacheitem = cacheitem;
        cacheitem->next = NULL;
        cacheitem->ref = 0;
        cacheitem->del_lock = 0;
        pthread_mutex_init(&(cacheitem->mutex), NULL);
        HeapItem* heapitem = (HeapItem*)malloc(sizeof(HeapItem));
        heapitem->cacheitem = cacheitem;
        heapitem->f = 1;
        heapitem->l = cachelist->cacheheap->threashold;
        CalculateK(heapitem, cachelist->cacheheap);
        cacheitem->heapitem = heapitem;
        if (cont_tmp->length > (cachelist->mem_free)) {
            /* remain locked
             * delete to replace*/
            if (cachelist->cacheheap->threashold
                > cachelist->cacheheap->array[0]->f) {
                if (LfuReplace(cachelist, cont_tmp) < 0) {
                    Error("LfuReplace", "webserver.log");
                    if ((lock_op & LOCK_LIST)) {
                        pthread_mutex_unlock(&(cachelist->mutex));
                    }
                    free(cont_tmp);
                    free(heapitem);
                    free(cacheitem);
                    return NULL;
                }
            } else {
                if ((lock_op & LOCK_LIST)) {
                    pthread_mutex_unlock(&(cachelist->mutex));
                }
                free(cont_tmp);
                free(heapitem);
                free(cacheitem);
                return NULL;
            }
        }
        heapitem->idx = cachelist->cacheheap->heap_size;
        MinHeapPush((void**)cachelist->cacheheap->array, heapitem,
            &(cachelist->cacheheap->heap_size), cachelist->cacheheap->heap_cmp,
            cachelist->cacheheap->heap_swap);
        if (heapitem
            != (HeapItem*)(cachelist->cacheheap->array[heapitem->idx])) {
            Error("addcacheitem 202 "
                  "heapitem!=(HeapItem*)(cachelist->cacheheap->array..) 202 ",
                "webserver.log");
        }
        if (cachelist->rear == NULL || cachelist->front == NULL) {
            cachelist->front = cacheitem;
            cacheitem->pre = NULL;
        } else {
            cachelist->rear->next = cacheitem;
            cacheitem->pre = cachelist->rear;
        }
        cachelist->rear = cacheitem;
        cachelist->mem_free -= cont_tmp->length;
        ++(cachelist->num);

        /* lock item
         * to get read file into cont */
        pthread_mutex_lock(&(cacheitem->mutex));
        pthread_mutex_lock(&(cacheitem->cont->mutex));
        AddItem(cachelist->table, file_name, cacheitem->cont);
        if ((lock_op & LOCK_LIST)) {
            pthread_mutex_unlock(&(cachelist->mutex));
        }

        /* while (__sync_lock_test_and_set(&(cacheitem->lock), 1)) {
         *     if (cacheitem->del_lock) {
         *         printf("AddCacheItem received del_lock\n");
         *         printf("%d\n", cachelist->mem_free);
         *         Error("AddCacheItem del_lock", "webserver.log");
         *         return NULL;
         *     }
         * } */
        /* deleting item has a higher priority */
        if (cacheitem->del_lock) {
            pthread_mutex_unlock(&(cacheitem->mutex));
            /* printf("AddCacheItem received del_lock\n");
             * printf("%d\n", cachelist->mem_free); */
            /* Error("AddCacheItem del_lock", "webserver.log"); */
            return NULL;
        }
#ifndef TEST
        int file_fd;
        if ((file_fd = open(file_name, O_RDONLY)) == -1) {
            /* char error_buffer[80];
             * sprintf(error_buffer, "file open Error file_name:%s\n",
             * file_name); Error(error_buffer, "webserver.log"); */
            if (DelCacheItem(cachelist, &cacheitem, 1, LOCK_LIST) < 0)
                Error("DelCacheItem 143 AddCacheItem", "webserver.log");
            /* printf("file open failed\n"); */
            /* __sync_synchronize();
             * cacheitem->lock = 0; */
            return NULL;
        }
        cacheitem->cont->file_name = copystring(file_name);
        (void)lseek(file_fd, (off_t)0, SEEK_SET);
        cacheitem->cont->address
            = calloc(((cont_tmp->length) + 10), sizeof(char));
        if (cacheitem->cont->address == NULL) {
            if (DelCacheItem(cachelist, &cacheitem, 1, LOCK_LIST) < 0)
                Error("DelCacheItem 154 AddCacheItem", "webserver.log");
            return NULL;
        }
        int nread = 0;
        int read_idx = 0;
        do {
            nread = read(file_fd, cacheitem->cont->address + read_idx,
                (cacheitem->cont->length - read_idx));
            read_idx += nread;
        } while (nread != -1 && nread != 0);
        close(file_fd);
#else
        cont_tmp->address = malloc(cont_tmp->length);
#endif
        if (cacheitem->cont->length != GetFileSize(file_name)) {
            Error("AddCacheItem 263 length", "webserver.log");
        }

        cacheitem->cont->is_ready = true;
        pthread_cond_signal(&(cacheitem->cont->cond));
        pthread_mutex_unlock(&(cacheitem->cont->mutex));
        pthread_mutex_unlock(&(cacheitem->mutex));
        /* __sync_synchronize();
         * cacheitem->lock = 0; */
        return cacheitem;
    }
}
int DelCacheItem(
    CacheList* cachelist, CacheItem** delitems, int del_num, int lock_op)
{
    /*delete the oldest item in the front*/
    if (cachelist == NULL || cachelist->front == NULL)
        return -1;
    else {
        /* lock list */
        if (cachelist->del_lock) {
            return -1;
        }
        if (lock_op & LOCK_LIST)
            pthread_mutex_lock(&(cachelist->mutex));

        if (cachelist->front == NULL) {
            if ((lock_op & LOCK_LIST)) {
                pthread_mutex_unlock(&(cachelist->mutex));
            }
            return -1;
        }

        /* lock item */

        /* inform */
        /* printf("calling DelCacheItem\n"); */
        CacheItem* tmp = NULL;
        for (int i = 0; i < del_num; ++i) {
            tmp = delitems[i];
            tmp->del_lock = 1;
            if (lock_op & LOCK_ITEM)
                pthread_mutex_lock(&(tmp->mutex));
            while (tmp->ref) {
            }
            __sync_synchronize();
            if (tmp == cachelist->rear) {
                cachelist->rear = tmp->pre;
                if (cachelist->rear)
                    cachelist->rear->next = NULL;
            }
            if (tmp == cachelist->front) {
                cachelist->front = tmp->next;
                if (cachelist->front->pre)
                    cachelist->front->pre = NULL;
            }

            if (tmp->pre)
                tmp->pre->next = tmp->next;
            if (tmp->next)
                tmp->next->pre = tmp->pre;

            --(cachelist->num);
            cachelist->mem_free += tmp->cont->length;
            if (tmp->cont->pair) {
                if (DelItemByPair(cachelist->table, tmp->cont->pair, 0) < 0) {
                    Error("DelItemByPair 222 DelCacheItem", "webserver.log");
                }
            } else {
                if (tmp->cont->file_name)
                    free(tmp->cont->file_name);
                if (tmp->cont->address)
                    free(tmp->cont->address);
                if (tmp->cont)
                    free(tmp->cont);
            }
            free(tmp->heapitem);
            pthread_mutex_destroy(&(tmp->mutex));
            free(tmp);
        }

        if ((lock_op & LOCK_LIST)) {
            pthread_mutex_unlock(&(cachelist->mutex));
        }
    }
    return 0;
}
/* int LruReplace(CacheList* cachelist, Content* new_cont)
 * {
 *     [> cachelist should be locked <]
 *     if (pthread_mutex_trylock(&(cachelist->mutex)) == 0) {
 *         pthread_mutex_unlock(&(cachelist->mutex));
 *         Error("Lru list lock", "webserver.log");
 *         return -1;
 *     } else {
 *         int mem_free = cachelist->mem_free;
 *         int total_size = mem_free;
 *         int i = 0;
 *         int t = new_cont->length;
 *         CacheItem** delitems = calloc(cachelist->num, sizeof(CacheItem*));
 *         CacheItem* cacheitem = cachelist->front;
 *         while (total_size < new_cont->length) {
 *             while (cacheitem != NULL) {
 *                 if (cacheitem->cont->length >= t && cacheitem->ref == 0) {
 *                     delitems[i++] = cacheitem;
 *                     total_size += (cacheitem->cont->length);
 *                 }
 *
 *                 if (total_size >= new_cont->length)
 *                     break;
 *                 cacheitem = cacheitem->next;
 *             }
 *             if (total_size < new_cont->length) {
 *                 t = t / 2;
 *                 total_size = mem_free;
 *                 i = 0;
 *                 cacheitem = cachelist->front;
 *             }
 *         }
 *         if (DelCacheItem(cachelist, delitems, i, LOCK_ITEM) < 0)
 *             Error("DelCacheItem 270 LruReplace", "webserver.log");
 *         free(delitems);
 *         return 0;
 *     }
 * }
 *  */
int LfuReplace(CacheList* cachelist, Content* new_cont)
{
    if (pthread_mutex_trylock(&(cachelist->mutex)) == 0) {
        pthread_mutex_unlock(&(cachelist->mutex));
        Error("Lfu list lock", "webserver.log");
        return -1;
    } else {
        int mem_free = cachelist->mem_free;
        int total_size = mem_free;
        cachelist->cacheheap->threashold = cachelist->cacheheap->array[0]->k;
        while (total_size < new_cont->length) {
            HeapItem* heapitem = MinHeapPop((void**)cachelist->cacheheap->array,
                &cachelist->cacheheap->heap_size,
                cachelist->cacheheap->heap_cmp,
                cachelist->cacheheap->heap_swap);
            if (heapitem->idx != cachelist->cacheheap->heap_size) {
                Error("LfuReplace 405 heapitem->idx!=", "webserver.log");
            }
            /* assert(cachelist->cacheheap->array[0]->f
             *     < cachelist->cacheheap->array[1]->f);
             * assert(cachelist->cacheheap->array[0]->f
             *     < cachelist->cacheheap->array[2]->f); */
            if (heapitem == NULL) {
                Error("LfuReplace 414 heapitem==NULL", "webserver.log ");
            }

            /* assert(MinHeapCheck((void**)cachelist->cacheheap->array,
             *            &cachelist->cacheheap->heap_size,
             *            cachelist->cacheheap->heap_cmp,
             *            cachelist->cacheheap->heap_swap)
             *     == 0); */
            total_size += heapitem->cacheitem->cont->length;
            if (DelCacheItem(cachelist, &(heapitem->cacheitem), 1, LOCK_ITEM)
                < 0)
                Error("DelCacheItem 270 LruReplace", "webserver.log");
        }
        return 0;
    }
    return 0;
}

int LfuRefer(CacheList* cachelist, CacheItem* cacheitem, int lock_op)
{
    __sync_add_and_fetch(&(cacheitem->ref), 1);
    if (cacheitem->del_lock || cachelist->del_lock) {
        __sync_sub_and_fetch(&(cacheitem->ref), 1);
        if (cacheitem->del_lock)
            return -1;
        return 0;
    }
    if (lock_op & LOCK_LIST) {
        while (pthread_mutex_trylock(&(cachelist->mutex)) != 0) {
            if (cacheitem->del_lock || cachelist->del_lock) {
                __sync_sub_and_fetch(&(cacheitem->ref), 1);
                if (cacheitem->del_lock)
                    return -1;
                return 0;
            }
        }
    }
    if (cacheitem->del_lock || cachelist->del_lock) {
        __sync_sub_and_fetch(&(cacheitem->ref), 1);
        if (lock_op & LOCK_LIST)
            pthread_mutex_unlock(&(cachelist->mutex));
        if (cacheitem->del_lock)
            return -1;
        return 0;
    }
    pthread_mutex_lock(&(cacheitem->mutex));
    if (cacheitem->del_lock || cachelist->del_lock) {
        __sync_sub_and_fetch(&(cacheitem->ref), 1);
        if (lock_op & LOCK_LIST)
            pthread_mutex_unlock(&(cachelist->mutex));
        pthread_mutex_unlock(&(cacheitem->mutex));
        if (cacheitem->del_lock)
            return -1;
        return 0;
    }
    ++(cacheitem->heapitem->f);
    CalculateK(cacheitem->heapitem, cachelist->cacheheap);
    if ((cacheitem->heapitem->k > MAX_INT || cacheitem->heapitem->f > MAX_INT
            || cacheitem->heapitem->l > MAX_INT))
        CutDownThreashold(cachelist->cacheheap);
    MinHeapModMid((void**)cachelist->cacheheap->array,
        &cachelist->cacheheap->heap_size, cacheitem->heapitem->idx,
        cachelist->cacheheap->heap_cmp, cachelist->cacheheap->heap_swap);

    pthread_mutex_unlock(&(cacheitem->mutex));
    if (lock_op & LOCK_LIST)
        pthread_mutex_unlock(&(cachelist->mutex));
    __sync_sub_and_fetch(&(cacheitem->ref), 1);
    return 0;
}

int MoveCacheItemToTail(CacheList* cachelist, CacheItem* cacheitem, int lock_op)
{
    __sync_add_and_fetch(&(cacheitem->ref), 1);
    if (cacheitem == cachelist->rear || cacheitem->del_lock
        || cachelist->del_lock) {
        __sync_sub_and_fetch(&(cacheitem->ref), 1);
        if (cacheitem->del_lock)
            return -1;
        return 0;
    }
    if (lock_op & LOCK_LIST) {
        pthread_mutex_lock(&(cachelist->mutex));
    }
    if (cacheitem == cachelist->rear || cacheitem->del_lock
        || cachelist->del_lock) {
        __sync_sub_and_fetch(&(cacheitem->ref), 1);
        if (lock_op & LOCK_LIST)
            pthread_mutex_unlock(&(cachelist->mutex));
        if (cacheitem->del_lock)
            return -1;
        return 0;
    }
    pthread_mutex_lock(&(cacheitem->mutex));
    if (cacheitem == cachelist->rear || cacheitem->del_lock
        || cachelist->del_lock) {
        __sync_sub_and_fetch(&(cacheitem->ref), 1);
        if (lock_op & LOCK_LIST)
            pthread_mutex_unlock(&(cachelist->mutex));
        pthread_mutex_unlock(&(cacheitem->mutex));
        if (cacheitem->del_lock)
            return -1;
        return 0;
    }

    if (cacheitem->next != NULL) {
        if (cacheitem == cachelist->front)
            cachelist->front = cacheitem->next;
        if (cacheitem->pre)
            cacheitem->pre->next = cacheitem->next;
        if (cacheitem->next)
            cacheitem->next->pre = cacheitem->pre;
        cachelist->rear->next = cacheitem;
        cacheitem->pre = cachelist->rear;
        cacheitem->next = NULL;
        cachelist->rear = cacheitem;
    }
    pthread_mutex_unlock(&(cacheitem->mutex));
    if (lock_op & LOCK_LIST)
        pthread_mutex_unlock(&(cachelist->mutex));
    __sync_sub_and_fetch(&(cacheitem->ref), 1);
    return 0;
}

/* unused function */
/* int ReadCacheItem(CacheList* cachelist, CacheItem* cacheitem, char* buffer)
 * {
 *     while (cacheitem->lock) {
 *         if (cachelist->del_lock)
 *             return -1;
 *     };
 *     cacheitem->ref += 1;
 *     snprintf(buffer, cacheitem->cont->length, "%s",
 * (cacheitem->cont->address)); MoveCacheItemToTail(cachelist, cacheitem,
 * LOCK_LIST);
 *     __sync_synchronize();
 *     cacheitem->ref -= 1;
 *     return 0;
 * } */

int CacheListDestroy(CacheList* cachelist)
{
    CacheItem** delitems;
    CacheItem* cacheitem = cachelist->front;
    delitems = malloc(sizeof(CacheItem*) * cachelist->num);
    int i = 0;

    cachelist->del_lock = 1;
    pthread_mutex_lock(&(cachelist->mutex));

    while (i < cachelist->num) {
        delitems[i++] = cacheitem;
        cacheitem = cacheitem->next;
    }
    if (DelCacheItem(cachelist, delitems, cachelist->num, LOCK_ITEM) < 0)
        Error("DelCacheItem 357 CacheListDestroy", "webserver.log");
    free(delitems);
    pthread_mutex_destroy(&(cachelist->mutex));
    free(cachelist);
    return 0;
}
