#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include "common.h"

#define ROUNDOFF(val, mul) (((val) + ((mul) - 1)) & ~((mul) - 1))

struct mapentry {
    void *addr;
    void *unraddr;
    size_t len;
    size_t unrlen;
    int prot_exec;
    struct mapentry *next;
};

static struct mapentry *cur_mapentry = NULL;

int mem_map(void *addr, size_t len, int prot_exec) {
    void *addr_align = (void*) ROUNDOFF((uintptr_t)addr, 0x10000);
    size_t len_align = ROUNDOFF(len, 0x10000);
    void *memmap;
    struct mapentry *mentry;
    int prot = PROT_READ | PROT_WRITE;
    if (prot_exec) prot |= PROT_EXEC;

    memmap = mmap(addr_align, len_align, prot, MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);
    if (memmap == NULL) {
        PRINT_DBG("> mem_map: Cannot allocate virtual memory address at %p with size 0x%x\n", addr, len);
        return 1;
    }

    mentry = malloc(sizeof(struct mapentry));
    mentry->addr = addr_align;
    mentry->unraddr = addr;
    mentry->len = len_align;
    mentry->unrlen = len;
    mentry->prot_exec = prot_exec;

    mentry->next = cur_mapentry;
    cur_mapentry = mentry;

    return 0;
}

void mem_unmap_all(void) {
    while (cur_mapentry != NULL) {
        struct mapentry *tmp_mentry = cur_mapentry->next;

        munmap(cur_mapentry->addr, cur_mapentry->len);
        free(cur_mapentry);
        cur_mapentry = tmp_mentry;
    }
}

static void *heap_addr = (void *) 0x010e0000;

void *get_heap_addr(void) {
    return heap_addr;
}

int heap_alloc(void *end_addr) {
    int ret;
    size_t heapsize;

    if (end_addr < heap_addr) {
        PRINT_DBG("> heap_alloc: Address %p < %p\n", end_addr, heap_addr);
        return 1;
    }
    heapsize = end_addr - heap_addr;

    ret = mem_map(heap_addr, heapsize, 1);
    if (ret) heap_addr = end_addr;
    return ret;
}
