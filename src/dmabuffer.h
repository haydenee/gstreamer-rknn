#ifndef _DMA_BUFFER_H_
#define _DMA_BUFFER_H_
#include <stddef.h>
int dmabuf_heap_open();
void dmabuf_heap_close(int heap_fd);
int dmabuf_heap_alloc(int heap_fd, const char *name, size_t size);
void* dmabuf_mmap(int fd, size_t size);
void dmabuf_munmap(void *ptr, size_t size);
#endif