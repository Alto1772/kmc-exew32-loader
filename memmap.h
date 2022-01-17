#ifndef EXE32_MEMMAP_H
#define EXE32_MEMMAP_H

int mem_map(void *, size_t);
void mem_unmap_all(void);
void print_map_entries(void);

void *get_heap_addr(void);
int heap_alloc(void *);

#endif // EXE32_MEMMAP_H
