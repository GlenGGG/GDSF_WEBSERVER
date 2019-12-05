#ifndef _MIN_HEAP
#define _MIN_HEAP
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdbool.h>
#include <assert.h>

void Swap(void** array, int i, int j);
void MinHeapModUp(void** array, uint32_t *heap_size, int cur_node,
                  int (*heap_cmp)(void* a, void* b), void (*heap_swap)(void** array, int a, int b));
void MinHeapModDown(void** array, uint32_t *heap_size, int cur_node,
                    int (*heap_cmp)(void* a, void* b), void (*heap_swap)(void** array, int a, int b));
void MinHeapModMid(void** array, uint32_t *heap_size, int cur_node,
                   int (*heap_cmp)(void* a, void* b), void (*heap_swap)(void** array, int a, int b));
void MinHeapCreate(void** array, uint32_t *heap_size,
                   int (*heap_cmp)(void* a, void* b), void (*heap_swap)(void** array, int a, int b));
void* MinHeapPop(void** array, uint32_t *heap_size,
                 int (*heap_cmp)(void* a, void* b), void (*heap_swap)(void** array, int a, int b));
void MinHeapPush(void** array,void* m, uint32_t *heap_size,
                 int (*heap_cmp)(void* a, void* b), void (*heap_swap)(void** array, int a, int b));
void MinHeapDelItem(void** array, uint32_t *heap_size, int del_node,
                    int (*heap_cmp)(void* a, void* b), void (*heap_swap)(void** array, int a, int b));
int MinHeapCheck(void** array, uint32_t* heap_size,
                 int (*heap_cmp)(void* a, void* b),
                 void (*heap_swap)(void** array, int a, int b));
#endif
