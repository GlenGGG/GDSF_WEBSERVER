#include "thread_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ThreadPool* InitThreadPool(int num_threads)
{
    ThreadPool* pool;
    pool = (ThreadPool*)malloc(sizeof(struct ThreadPool));
    pool->num_threads = 0;
    pool->num_working = 0;
    pthread_mutex_init(&(pool->thcount_lock), NULL);
    pthread_cond_init(&(pool->threads_all_idle), NULL);

    if (InitTaskQueue(&pool->queue) < 0) {
        Error("InitTaskQueue", "thread_pool");
        return NULL;
    }

    pool->threads
        = (struct Thread**)malloc(num_threads * sizeof(struct Thread*));

    pool->is_alive = true;
    for (int i = 0; i < num_threads; ++i) {
        CreateThread(pool, &(pool->threads[i]), i);
    }

    while (pool->num_threads != num_threads) {
    }

    return pool;
}

void AddTaskToThreadPool(ThreadPool* pool, Task* curtask)
{
    PushTaskQueue(&pool->queue, curtask);
}

void WaitThreadPool(ThreadPool* pool)
{
    pthread_mutex_lock(&pool->thcount_lock);
    while (pool->queue.len || pool->num_working) {
        pthread_cond_wait(&pool->threads_all_idle, &pool->thcount_lock);
    }
    pthread_mutex_unlock(&pool->thcount_lock);
}

int DestroyThreadPool(ThreadPool* pool)
{
    int num; // stored num_threads
    pthread_mutex_lock(&(pool->thcount_lock));
    num = pool->num_threads;
    pthread_mutex_unlock(&(pool->thcount_lock));

    pool->is_alive = false;
    WaitThreadPool(pool);
    if (DestroyTaskQueue(&pool->queue) < 0)
        return -1;

    for (int i = 0; i < num; ++i)
        free(pool->threads[i]);

    pthread_mutex_destroy(&(pool->thcount_lock));
    pthread_cond_destroy(&(pool->threads_all_idle));

    free(pool->threads);

    return 0;
}

int GetNumOfThreadWorking(ThreadPool* pool) { return pool->num_working; }

int CreateThread(struct ThreadPool* pool, struct Thread** pthread, int id)
{
    *pthread = (struct Thread*)malloc(sizeof(struct Thread));
    if (*pthread == NULL) {
        Error("CreateThread(): Could not allocate memory for Thread\n",
            "thread_pool.log");
        return -1;
    }

    (*pthread)->pool = pool;
    (*pthread)->id = id;

    pthread_create(&((*pthread)->pthread), NULL, (void*)ThreadDo, (*pthread));
    pthread_detach((*pthread)->pthread);

    return 0;
}

void* ThreadDo(struct Thread* pthread)
{
    char thread_name[128] = { 0 };
    char log_name[NAME_LEN];
    struct ThreadInfo threadinfo;
    struct LogFile logf;
    struct TimeCounter counter;
    sprintf(thread_name, "thread-pool-%d", pthread->id);
    sprintf(log_name, SERVER_CODE "-%s.log", thread_name);
    prctl(PR_SET_NAME, thread_name);
    memset(&(counter.cost_time), 0, sizeof(counter.cost_time));
    counter.invokes = 0;
    counter.total_cost_time = 0;
    pthread_mutex_init(&(counter.mutex), NULL);
    threadinfo.counter = &counter;

    LogFileInit(&logf, log_name);
    threadinfo.id = &(pthread->id);
    threadinfo.logf = &logf;
    threadinfo.thread_name = thread_name;

    pthread->threadinfo = &threadinfo;
    ThreadPool* pool = pthread->pool;

    pthread_mutex_lock(&(pool->thcount_lock));
    ++(pool->num_threads);
    pthread_mutex_unlock(&(pool->thcount_lock));

    while (pool->is_alive) {

        pthread_mutex_lock(&(pool->queue.has_jobs->mutex));
        /*always check if pool is still alive*/
        while (pool->is_alive && !(pool->queue.has_jobs->status)) {
            pthread_cond_wait(
                &(pool->queue.has_jobs->cond), &(pool->queue.has_jobs->mutex));
        }
        pthread_mutex_unlock(&(pool->queue.has_jobs->mutex));

        if (pool->is_alive) {

            pthread_mutex_lock(&(pool->thcount_lock));
            ++(pool->num_working);
            pthread_mutex_unlock(&(pool->thcount_lock));

            void* (*func)(void*);
            void* arg;

            Task* curtask = TakeTaskQueue(&pool->queue);
            if (curtask) {
                func = curtask->function;
                arg = (curtask->arg);
                (*(ThreadInfo**)(arg)) = &(threadinfo);
                func(arg);
                free(curtask->arg);
                free(curtask);
            }
        }
        pthread_mutex_lock(&(pool->thcount_lock));
        --(pool->num_working);
        if (pool->num_working == 0)
            pthread_cond_signal(&(pool->threads_all_idle));
        pthread_mutex_unlock(&(pool->thcount_lock));
    }

    pthread_mutex_lock(&(pool->thcount_lock));
    --(pool->num_threads);
    pthread_mutex_unlock(&(pool->thcount_lock));

    return NULL;
}
int InitTaskQueue(struct TaskQueue* queue)
{
    pthread_mutex_init(&queue->mutex, NULL);
    queue->front = NULL;
    queue->rear = NULL;
    queue->len = 0;
    if ((queue->has_jobs = (Staconv*)malloc(sizeof(Staconv))) == NULL)
        return -1;
    queue->has_jobs->status = 0;
    pthread_mutex_init((&queue->has_jobs->mutex), NULL);
    pthread_cond_init((&queue->has_jobs->cond), NULL);
    return 0;
}
int PushTaskQueue(struct TaskQueue* queue, struct Task* curtask)
{
    bool is_empty = false; /*check if the queue is empty*/
    if (queue == NULL || curtask == NULL || queue == NULL
        || queue->has_jobs == NULL)
        return -1;

    pthread_mutex_lock(&queue->mutex);
    if (queue->front == NULL) {
        is_empty = true;
        queue->front = curtask;
        queue->rear = curtask;
        curtask->pre = NULL;
    } else {
        queue->rear->next = curtask;
        curtask->pre = queue->rear;
        queue->rear = curtask;
    }
    curtask->next = NULL;
    ++(queue->len);
    pthread_mutex_unlock(&queue->mutex);

    if (is_empty) {
        pthread_mutex_lock(&(queue->has_jobs->mutex));
        queue->has_jobs->status = 1;
        pthread_cond_broadcast(&(queue->has_jobs->cond));
        pthread_mutex_unlock(&(queue->has_jobs->mutex));
    }
    return 0;
}
int DestroyTaskQueue(struct TaskQueue* queue)
{
    pthread_mutex_lock(&(queue->mutex));
    if (queue->front != NULL || queue->rear != NULL || queue->len != 0) {
        pthread_mutex_unlock(&(queue->mutex));
        return -1;
    }
    pthread_mutex_unlock(&(queue->mutex));

    pthread_mutex_destroy(&(queue->has_jobs->mutex));
    pthread_cond_destroy(&(queue->has_jobs->cond));
    free(queue->has_jobs);
    pthread_mutex_destroy(&(queue->mutex));

    return 0;
}
Task* TakeTaskQueue(struct TaskQueue* queue)
{
    struct Task* t;
    bool is_empty = false;
    pthread_mutex_lock(&(queue->mutex));
    if (queue->front == NULL || queue->rear == NULL)
        is_empty = true;
    else {
        /*         t = queue->rear;
         *         queue->rear = queue->rear->pre;
         *         --(queue->len);
         *
         *         [>if queue is empty after pop<]
         *         if (queue->rear == NULL) {
         *             queue->front = NULL;
         *             pthread_mutex_lock(&(queue->has_jobs->mutex));
         *             queue->has_jobs->status = 0;
         *             pthread_mutex_unlock(&(queue->has_jobs->mutex));
         *         } else {
         *             queue->rear->next = NULL;
         *         }
         *         t->next = NULL;
         *         t->pre = NULL; */
        t = queue->front;
        queue->front = queue->front->next;
        --(queue->len);
        if (queue->front == NULL) {
            queue->rear = NULL;
            pthread_mutex_lock(&(queue->has_jobs->mutex));
            queue->has_jobs->status = 0;
            pthread_mutex_unlock(&(queue->has_jobs->mutex));
        } else {
            queue->front->pre = NULL;
        }
        t->next = NULL;
        t->pre = NULL;
    }
    pthread_mutex_unlock(&(queue->mutex));
    if (is_empty)
        return NULL;
    return t;
}
void Error(const char* msg, const char* log_name)
{
    struct LogFile logf;
    LogFileInit(&logf, log_name);
    Logger(ERROR, msg, "", 0, 1, &logf);
}
