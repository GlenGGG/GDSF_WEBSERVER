#ifndef _THREAD_POOL_H
#define _THREAD_POOL_H
#include <pthread.h>
#include <stdbool.h>
#include <sys/prctl.h>
#include "webserver.h"

#define THREAD_NUM 17
#define LOAD_FILE_THREAD_NUM 8


typedef  struct Staconv {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int status;
} Staconv;

typedef struct Task {
    struct Task* next;
    struct Task* pre;
    void* (*function)(void* arg);
    void* arg;
} Task;

typedef struct TaskQueue {
    pthread_mutex_t mutex;
    Task* front;
    Task* rear;
    Staconv* has_jobs;
    int len;
} TaskQueue;

/*used for logging separatly*/
typedef struct ThreadInfo {
    struct LogFile* logf;
    const char* thread_name;
    const int* id;
    TimeCounter* counter;
} ThreadInfo;

typedef struct Thread {
    int id;
    pthread_t pthread;
    struct ThreadPool* pool;
    struct ThreadInfo* threadinfo;
} Thread;

typedef struct ThreadPool {
    Thread** threads;
    volatile int num_threads;
    volatile int num_working;
    pthread_mutex_t thcount_lock;
    pthread_cond_t threads_all_idle;
    TaskQueue queue;
    volatile bool is_alive;
} ThreadPool;

void Error(const char* msg, const char* log_name);
struct ThreadPool* InitThreadPool(int num_threas);
void AddTaskToThreadPool(ThreadPool* pool, Task* curtask);
void WaitThreadPool(ThreadPool* pool);
int DestroyThreadPool(ThreadPool* pool);
int GetNumOfThreadWorking(ThreadPool* pool);
int CreateThread(struct ThreadPool* pool, struct Thread** pthread, int id);
void* ThreadDo(struct Thread* pthread);
int InitTaskQueue(struct TaskQueue* queue);
int PushTaskQueue(struct TaskQueue* queue, struct Task* curtask);
int DestroyTaskQueue(struct TaskQueue* queue);
Task* TakeTaskQueue(struct TaskQueue* queue);

#endif
