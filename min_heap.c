#include "min_heap.h"
#include <stdio.h>

void Swap(void** array, int i, int j)
{
    void* tmp;
    tmp = array[j];
    array[j] = array[i];
    array[i] = tmp;
}

void MinHeapDelItem(void** array, uint32_t* heap_size, int del_node,
    int (*heap_cmp)(void* a, void* b),
    void (*heap_swap)(void** array, int a, int b))
{
    if (*heap_size <= 0)
        return;
    assert(del_node >= 0);
    assert(heap_cmp != NULL);
    assert(array != NULL);
    heap_swap(array, del_node, *heap_size - 1);
    array[*heap_size - 1] = 0;
    --(*heap_size);
    MinHeapModDown(array, heap_size, del_node, heap_cmp, heap_swap);
}

void MinHeapModUp(void** array, uint32_t* heap_size, int cur_node,
    int (*heap_cmp)(void* a, void* b),
    void (*heap_swap)(void** array, int a, int b))
{
    if (*heap_size <= 0)
        return;
    assert(cur_node >= 0);
    assert(heap_cmp != NULL);
    assert(array != NULL);
    int father, min;
    father = (cur_node - 1) / 2;
    while (cur_node > 0 && father >= 0) {
        if (heap_cmp(array[father], array[cur_node]) > 0)
            min = cur_node;
        else
            min = father;
        if (min == cur_node) {
            heap_swap(array, min, father);
            cur_node = father;
            father = (father - 1) / 2;
        } else
            break;
    }
}
void MinHeapModDown(void** array, uint32_t* heap_size, int cur_node,
    int (*heap_cmp)(void* a, void* b),
    void (*heap_swap)(void** array, int a, int b))
{
    if (*heap_size <= 0)
        return;
    assert(cur_node >= 0);
    assert(heap_cmp != NULL);
    assert(array != NULL);
    int child, min;
    child = 2 * cur_node + 1;
    while (child < *heap_size) {
        if (heap_cmp(array[child], array[cur_node]) < 0)
            min = child;
        else
            min = cur_node;
        ++child;
        if (child < *heap_size && heap_cmp(array[child], array[min]) < 0)
            min = child;
        if (min != cur_node) {
            heap_swap(array, min, cur_node);
            cur_node = min;
            child = 2 * min + 1;
        } else
            break;
    }
}

void MinHeapModMid(void** array, uint32_t* heap_size, int cur_node,
    int (*heap_cmp)(void* a, void* b),
    void (*heap_swap)(void** array, int a, int b))
{
    if (*heap_size <= 0)
        return;
    assert(array != NULL);
    assert(cur_node >= 0);
    assert(heap_cmp != NULL);
    MinHeapModUp(array, heap_size, cur_node, heap_cmp, heap_swap);
    MinHeapModDown(array, heap_size, cur_node, heap_cmp, heap_swap);
}

void MinHeapCreate(void** array, uint32_t* heap_size,
    int (*heap_cmp)(void* a, void* b),
    void (*heap_swap)(void** array, int a, int b))
{
    int i;
    assert(array != NULL);
    assert(heap_cmp != NULL);
    for (i = (*heap_size - 1) / 2; i >= 0; i--) {
        MinHeapModDown(array, heap_size, i, heap_cmp, heap_swap);
    }
}

void* MinHeapPop(void** array, uint32_t* heap_size,
    int (*heap_cmp)(void* a, void* b),
    void (*heap_swap)(void** array, int a, int b))
{
    assert(heap_cmp != NULL);
    if (*heap_size <= 0)
        return NULL;
    assert(array != NULL);
    void* target = array[0];
    assert(target != NULL);
    MinHeapDelItem(array, heap_size, 0, heap_cmp, heap_swap);
    assert(target != NULL);
    return target;
}

void MinHeapPush(void** array, void* m, uint32_t* heap_size,
    int (*heap_cmp)(void* a, void* b),
    void (*heap_swap)(void** array, int a, int b))
{
    assert(heap_cmp != NULL);
    assert(array != NULL);
    ++(*heap_size);
    array[*heap_size - 1] = m;
    MinHeapModUp(array, heap_size, *heap_size - 1, heap_cmp, heap_swap);
}
int MinHeapCheck(void** array, uint32_t* heap_size,
    int (*heap_cmp)(void* a, void* b),
    void (*heap_swap)(void** array, int a, int b))
{
    for (int i = 0; i < *heap_size; i++) {
        int child = 2 * i + 1;
        if (child < *heap_size)
            if (heap_cmp(array[child], array[i]) < 0) {
                fprintf(stderr, "%d is smaller than %d in array\n", child, i);
                return -1;
            }
        ++child;
        if (child < *heap_size)
            if (heap_cmp(array[child], array[i]) < 0) {
                fprintf(stderr, "%d is smaller than %d in array\n", child, i);
                return -1;
            }
    }
    return 0;
}
