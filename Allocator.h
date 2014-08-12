#ifndef ALLOCATOR_H
#define ALLOCATOR_H

void setupHeap(void *start, size_t heapSize);
void *xMalloc(size_t bytes);
void xFree(void *pnt);

#endif
