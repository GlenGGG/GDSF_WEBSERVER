#include "hashtable.h"
#include "memswitch.h"
#include "thread_pool.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#define _djb2_str_hash

CacheList* cachelist;
long long hit = 0;
long long loss = 0;
long long collide = 0;
long long collide_length = 0;

static inline unsigned long int hashString(const char* str)
{
#ifdef _djb2_str_hash
    unsigned long hash = 5381;
    int c;

    while ((c = *str++) && (c != '.'))
        hash = ((hash << 5) + hash) + c;
    return hash;
#endif
    return 0;
}

inline char* copystring(char* value)
{
    char* copy = (char*)calloc(strlen(value) + 1, sizeof(char));
    if (!copy) {
        printf("Unable to allocte string value %s\n", value);
        abort();
    }
    strncpy(copy, value, strlen(value));
    return copy;
}

static inline int isEqualContent(Content* cont1, Content* cont2)
{
    if (cont1 == NULL || cont2 == NULL)
        return 0;
    if (cont1->length != cont2->length)
        return 0;
    if (cont1->address != cont2->address)
        return 0;
    return 1;
}

HashTable* CreateHashTable(int num_bucket)
{
    HashTable* table = (HashTable*)malloc(sizeof(HashTable));
    if (NULL == table) {
        return NULL;
    }
    table->bucket = (HashPair**)calloc(num_bucket, sizeof(void*));
    if (!table->bucket) {
        free(table);
        return NULL;
    }
    table->num_bucket = num_bucket;

#ifdef HASHTHREAD
    /* table->locks = (int*)calloc(num_bucket, sizeof(int)); */
    table->mutex
        = (pthread_mutex_t*)calloc(num_bucket, sizeof(pthread_mutex_t));
    if (!table->mutex) {
        free(table);
        return NULL;
    }
    for (int i = 0; i < num_bucket; ++i)
        pthread_mutex_init(&(table->mutex[i]), NULL);
#endif

    return table;
}

void FreeHashTable(HashTable* table)
{
    if (table == NULL)
        return;
    HashPair* next;
    for (int i = 0; i < (table->num_bucket); ++i) {
        HashPair* pair = table->bucket[i];
        while (pair) {
            next = pair->next;
            free(pair->key);
            free(pair->cont->address);
            free(pair->cont);
            free(pair);
            pair = next;
        }
    }

    free(table->bucket);
#ifdef HASHTHREAD
    free((void*)(table->mutex));
    /* free((void*)(table->locks)); */
#endif
    free(table);
}

int AddItem(HashTable* table, char* key, Content* cont)
{
    int hash = hashString(key) % (table->num_bucket);
    HashPair* pair = table->bucket[hash];
#ifdef HASHTHREAD
    pthread_mutex_lock(&(table->mutex[hash]));
    /* while (__sync_lock_test_and_set(&table->locks[hash], 1)) {
     * } */
#endif
    while (pair != 0) {
        if (0 == strcmp(pair->key, key) && isEqualContent(pair->cont, cont)) {
            /* __sync_synchronize();
             * table->locks[hash] = 0; */
            pthread_mutex_unlock(&(table->mutex[hash]));
            return 1;
        }
        if (0 == strcmp(pair->key, key) && !isEqualContent(pair->cont, cont)) {
            if (!pair->cont->cacheitem->del_lock) {
                pair->cont->pair = NULL;
                if (DelCacheItem(
                        cachelist, &(pair->cont->cacheitem), 1, LOCK_LIST)
                    < 0)
                    Error("DelCacheItem 130 AddItem", "webserver.log");
            }

            pair->cont = cont;
            cont->pair = pair;
            pthread_mutex_unlock(&(table->mutex[hash]));
            return 0;
        }
        pair = pair->next;
    }
    pair = (HashPair*)malloc(sizeof(HashPair));
    pair->key = NULL;
    pair->key = copystring(key);
    pair->cont = cont;
    cont->pair = pair;
    pair->next = table->bucket[hash];
    if (table->bucket[hash])
        table->bucket[hash]->pre = pair;
    table->bucket[hash] = pair;
    pair->pre = NULL;
    pair->hash = hash;

#ifdef HASHTHREAD
    pthread_mutex_unlock(&(table->mutex[hash]));
#endif
    return 2;
}

int DelItemByPair(HashTable* table, HashPair* pair, int lock_op)
{
    if (pair == 0)
        return -1;
    /* while (__sync_lock_test_and_set(&table->locks[hash], 1)) {
     * } */
    if (lock_op & LOCK_PAIR)
        pthread_mutex_lock(&(table->mutex[pair->hash]));
    if (pair == table->bucket[pair->hash]) {
        table->bucket[pair->hash] = pair->next;
        if (pair->next)
            pair->next->pre = NULL;
    } else {
        pair->pre->next = pair->next;
        if (pair->next)
            pair->next->pre = pair->pre;
    }
    free(pair->key);
    free(pair->cont->address);
    free(pair->cont->file_name);
    free(pair->cont);
    free(pair);
    return 0;
}
int DelItem(HashTable* table, char* key)
{
    int hash = hashString(key) % (table->num_bucket);

    HashPair* pair = table->bucket[hash];
    HashPair* prev = NULL;
    if (pair == 0)
        return -1;
#ifdef HASHTHREAD
    /* while (__sync_lock_test_and_set(&table->locks[hash], 1)) {
     * } */
    pthread_mutex_lock(&(table->mutex[hash]));
#endif
    while (pair != 0) {
        if (0 == strcmp(pair->key, key)) {
            if (!prev) {
                table->bucket[hash] = pair->next;
                if (pair->next)
                    pair->next->pre = NULL;
            } else {
                prev->next = pair->next;
                if (pair->next)
                    pair->next->pre = prev;
            }
            free(pair->key);
            free(pair->cont->address);
            free(pair->cont->file_name);
            free(pair->cont);
            free(pair);
            pthread_mutex_unlock(&(table->mutex[hash]));
            return 0;
        }
        prev = pair;
        pair = pair->next;
    }
#ifdef HASHTHREAD
    pthread_mutex_unlock(&(table->mutex[hash]));
#endif
    return -1;
}

Content* GetContentByKey(HashTable* table, char* key)
{
    int hash = hashString(key) % (table->num_bucket);
    HashPair* pair = table->bucket[hash];

    while (pair) {
        if (0 == strcmp(pair->key, key)) {
            __sync_add_and_fetch(&(pair->cont->cacheitem->ref), 1);

            /* wait till cont is ready to read */
            pthread_mutex_lock(&(pair->cont->mutex));
            while (!(pair->cont->is_ready)) {
                pthread_cond_wait(&(pair->cont->cond), &(pair->cont->mutex));
            }
            pthread_mutex_unlock(&(pair->cont->mutex));

            if (LfuRefer(cachelist, pair->cont->cacheitem, LOCK_LIST) < 0) {
                /* Error(
                 *     "LfuRefer 241 GetContentByKey", "webserver.log"); */
                __sync_sub_and_fetch(&(pair->cont->cacheitem->ref), 1);
                break;
            }
            __sync_add_and_fetch(&hit, 1);
            if (pair != table->bucket[hash])
                __sync_add_and_fetch(&collide, 1);
            __sync_sub_and_fetch(&(pair->cont->cacheitem->ref), 1);
            __sync_synchronize();
            return pair->cont;
        }
        __sync_add_and_fetch(&collide_length, 1);
        pair = pair->next;
    }
    CacheItem* cacheitem = NULL;
    __sync_add_and_fetch(&loss, 1);
    if ((cachelist->cacheheap->threashold) > MAX_INT) {
        pthread_mutex_lock(&(cachelist->mutex));
        CutDownThreashold(cachelist->cacheheap);
        pthread_mutex_unlock(&(cachelist->mutex));
    }
    cacheitem = AddCacheItem(cachelist, NULL, key, LOCK_LIST);
    if (cacheitem == NULL) {
        return NULL;
    }
    if (GetFileSize(key) != cacheitem->cont->length) {
        Error("GetContentByKey 260 file_length complication", "webserver.log");
    }
    return cacheitem->cont;
}

HashPair* FindHashByKeyNoAdd(HashTable* table, char* key)
{
    int hash = hashString(key) % (table->num_bucket);
    HashPair* pair = table->bucket[hash];

    while (pair) {
        if (0 == strcmp(pair->key, key)) {
            pthread_mutex_lock(&(pair->cont->mutex));
            while (!pair->cont->is_ready) {
                pthread_cond_wait(&(pair->cont->cond), &(pair->cont->mutex));
            }
            pthread_mutex_unlock(&(pair->cont->mutex));
            return pair;
        }
        pair = pair->next;
    }
    return NULL;
}
/* void* thread_func(void* arg)
 * {
 *     threadinfo* info = arg;
 *     char buffer[512];
 *     long long i = info->start;
 *     HashTable* table = info->table;
 *     free(info);
 *     for (; i < HASHCOUNT; i += NUMTHREADS) {
 *         sprintf(buffer, "%lld", i);
 *         CacheItem* cacheitem;
 *         cacheitem = AddCacheItem(cachelist, NULL, NULL, true);
 *         [> printf("%d is dong %lld\n", (int)(i&0x7), i); <]
 *         AddItem(table, buffer, cacheitem->cont);
 *     }
 *     return NULL;
 * } */

/* int main(void)
 * {
 *     HashTable* table = CreateHashTable(HASHCOUNT);
 *     CacheListInit(&cachelist, MEM_MAX, table);
 *     srand((unsigned)time(NULL));
 *     struct timeval tval_before, tval_done1, tval_done2, tval_writehash,
 *         tval_readhash;
 *     gettimeofday(&tval_before, NULL);
 *     int t;
 *     pthread_t* threads[NUMTHREADS];
 *     for (t = 0; t < NUMTHREADS; ++t) {
 *         pthread_t* pth = malloc(sizeof(pthread_t));
 *         threads[t] = pth;
 *         threadinfo* info = (threadinfo*)malloc(sizeof(threadinfo));
 *         info->table = table;
 *         info->start = t;
 *         pthread_create(pth, NULL, thread_func, info);
 *     }
 *     for (t = 0; t < NUMTHREADS; ++t) {
 *         pthread_join(*threads[t], NULL);
 *     }
 *     gettimeofday(&tval_done1, NULL);
 *     int i;
 *     char buffer[512];
 *     for (i = 0; i < HASHCOUNT; ++i) {
 *         sprintf(buffer, "%d", i);
 *         GetContentByKey(table, buffer);
 *     }
 *     gettimeofday(&tval_done2, NULL);
 *     timersub(&tval_done1, &tval_before, &tval_writehash);
 *     timersub(&tval_done2, &tval_done1, &tval_readhash);
 *     printf("\n%d threads. \n", NUMTHREADS);
 *     printf("Store %d ints by string: %ld.%06ld sec, read %d ints: %ld.%06ld "
 *            "sec\n",
 *         HASHCOUNT, (long int)tval_writehash.tv_sec,
 *         (long int)tval_writehash.tv_usec, HASHCOUNT,
 *         (long int)tval_readhash.tv_sec, (long int)tval_readhash.tv_usec);
 *     printf("h: %.2lf%%\nhit: %lld\nloss: %lld\n", ((double)hit*100 / (hit +
 * loss)), hit, loss); printf("num: %lld\n", cachelist->num); printf("done\n");
 *     FreeHashTable(table);
 *     return 0;
 * } */
