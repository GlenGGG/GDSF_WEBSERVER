#ifndef _HASHTABLE_H
#define _HASHTABLE_H
#define HASHTHREAD
#define NUMTHREADS 8
#define HASHCOUNT 100000
#ifdef HASHTHREAD
#include <pthread.h>
#include <stdbool.h>
#endif

typedef struct Content {
    int length;
    char* file_name;
    char* address;
    struct HashPair* pair;
    struct CacheItem* cacheitem;
    pthread_mutex_t mutex;      //lock before it's ready to read
    pthread_cond_t cond;
    bool is_ready;              //whether cont is ready to read
} Content;

typedef struct HashPair {
    char* key;
    unsigned int hash;
    Content* cont;
    struct HashPair* next;
    struct HashPair* pre;
} HashPair;

typedef struct HashTable {
    HashPair** bucket;
    unsigned int num_bucket;
#ifdef HASHTHREAD
    /* volatile int* locks; */
    pthread_mutex_t* mutex;
#endif
} HashTable;

typedef struct LoaderThreadInfo {
    HashTable* table;
    int begin_idx;
    int max_cnt;
} LoaderThreadInfo;


HashTable* CreateHashTable(int num_bucket);
void FreeHashTable(HashTable* table);
int AddItem(HashTable* table, char* key, Content* cont);
int DelItem(HashTable* table, char* key);
int DelItemByPair(HashTable* table, HashPair* pair, int lock_op);
char* copystring(char* value);
Content* GetContentByKey(HashTable* table, char* key);
HashPair* FindHashByKeyNoAdd(HashTable* table, char* key);


#endif
