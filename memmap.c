#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/mman.h>
#include "common.h"

#define ROUNDOFF(val, mul) (((val) + ((mul) - 1)) & ~((mul) - 1))

struct mapentry {
    uintptr_t addr;
    size_t len;
    int prot;
    struct mapentry *next;
};

static struct mapentry *mentry_head = NULL, *mentry_tail;

static void mentry_add_node(struct mapentry *mentry) {
    mentry->next = NULL;

    if (mentry_head == NULL) {

        mentry_head = mentry;
        mentry_tail = mentry;
    }
    else {
        mentry_tail->next = mentry;
        mentry_tail = mentry;
    }
}

static struct mapentry *mentry_indexof_addr(uintptr_t addr) {
    struct mapentry *mentry;

    for (mentry = mentry_head; mentry != NULL; mentry = mentry->next) {
        if (addr == mentry->addr)
            return mentry;
    }

    return NULL;
}

static struct mapentry *mentry_find_lowest_addr(uintptr_t loaddr_limit) {
    struct mapentry *mentry, *retment = NULL;
    uintptr_t loaddr = 0;

    for (mentry = mentry_head; mentry != NULL; mentry = mentry->next) {
        if (loaddr_limit <= mentry->addr &&
                (loaddr == 0 || loaddr >= mentry->addr)) {
            loaddr = mentry->addr;
            retment = mentry;
        }
    }

    return retment;
}

static struct mapentry *mentry_find_highest_addr(uintptr_t hiaddr_limit) {
    struct mapentry *mentry, *retment = NULL;
    uintptr_t hiaddr = 0;

    for (mentry = mentry_head; mentry != NULL; mentry = mentry->next) {
        if (hiaddr_limit >= mentry->addr &&
                (hiaddr == 0 || hiaddr <= mentry->addr)) {
            hiaddr = mentry->addr;
            retment = mentry;
        }
    }

    return retment;
}

static int mentry_is_in_address_range(uintptr_t addr, size_t len) {
    uintptr_t start_addr = addr, end_addr = (uintptr_t) (addr + len);
    struct mapentry *mentry;

    for (mentry = mentry_head; mentry != NULL; mentry = mentry->next) {
        uintptr_t mstart_addr = mentry->addr, mend_addr = (uintptr_t) (mentry->addr + mentry->len);

        //  a ---[+++++++++++]---
        //  m ------[+++++]------
        if (start_addr <= mstart_addr && end_addr >= mend_addr)
            return 1;

        //  a ------[+++++]------
        //  m ---[+++++++++++]---
        else if (start_addr >= mstart_addr && end_addr <= mend_addr)
            return 1;

        //  a ---[++++++]--------
        //  m -------[+++++++]---
        else if (start_addr <= mstart_addr && start_addr < mend_addr &&
                end_addr > mstart_addr && end_addr <= mend_addr)
            return 1;

        //  a -------[+++++++]---
        //  m ---[++++++]--------
        else if (start_addr >= mstart_addr && start_addr < mend_addr &&
                end_addr > mstart_addr && end_addr >= mend_addr)
            return 1;
    }

    return 0;
}

static int _mem_map(uintptr_t addr, size_t len, int prot) {
    struct mapentry *mentry;

    if (mmap((void *) addr, len, prot, MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0) == NULL) {
        PRINT_DBG("> mem_map: Cannot allocate virtual memory address at 0x%"PRIxPTR" with size 0x%x\n", addr, len);
        return 1;
    }

    mentry = malloc(sizeof(struct mapentry));
    mentry->addr = addr;
    mentry->len = len;
    mentry->prot = prot;

    mentry_add_node(mentry);
    return 0;
}

static int split_cut_map(uintptr_t addr, size_t len, int prot) {
    struct mapentry *lo_mentry, *hi_mentry;

    while (1) {
        hi_mentry = mentry_find_highest_addr(addr + len);
        lo_mentry = mentry_find_lowest_addr(addr);

        // if addr is inside hi_mentry's range
        if (hi_mentry != NULL && addr > hi_mentry->addr && addr < hi_mentry->addr + hi_mentry->len) {
            len -= hi_mentry->addr + hi_mentry->len - addr;
            addr = hi_mentry->addr + hi_mentry->len;
        }

        if (lo_mentry == NULL)
            return _mem_map(addr, len, prot);

        // if it's already in the list
        else if (lo_mentry->addr == addr) {
            // if the found entry's size is smaller than this size
            if (lo_mentry->len >= len)
                return 0;

            // or if is bigger than the entry's
            else if (lo_mentry->len < len) {
                addr += lo_mentry->len;
                len -= lo_mentry->len;
            }
        }

        // if entry's addr is way ahead
        else if (lo_mentry->addr > addr) {
            if (_mem_map(addr, lo_mentry->addr - addr, prot))
                return 1;

            len -= lo_mentry->addr - addr;
            addr = lo_mentry->addr;
        }
    }

    return 1; // just in case the compiler doesn't want no return after the loop
}

int mem_map(void *addr, size_t len, int prot_exec) {
    uintptr_t _addr = ROUNDOFF((uintptr_t)addr, 0x10000);
    size_t _len = ROUNDOFF(len, 0x10000);
    int prot = PROT_READ | PROT_WRITE;
    if (prot_exec) prot |= PROT_EXEC;

    if (mentry_is_in_address_range(_addr, _len)) {
        return split_cut_map(_addr, _len, prot);
    }
    else
        return _mem_map(_addr, _len, prot);
}

void mem_unmap_all(void) {
    while (mentry_head != NULL) {
        struct mapentry *tmp_mentry = mentry_head->next;

        munmap((void *) mentry_head->addr, mentry_head->len);
        free(mentry_head);
        mentry_head = tmp_mentry;
    }
}

void print_map_entries(void) {
    struct mapentry *mentry;

    PRINT_ERR("> Memory Map Entries:\n");
    PRINT_ERR("    Address      Size\n");
    for (mentry = mentry_head; mentry != NULL; mentry = mentry->next) {
        PRINT_ERR("    0x%"PRIxPTR"    0x%x\n", mentry->addr, mentry->len);
    }
}

static void *heap_addr = (void *) 0x01000000;

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

    ret = mem_map(heap_addr, heapsize, 0);
    if (ret) heap_addr = end_addr;
    return ret;
}
