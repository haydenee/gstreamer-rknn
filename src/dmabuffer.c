#include "dmabuffer.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "stdio.h"
#include <sys/mman.h>

int dma_heap_fd = -1;
int dma_buf_opened = 0;

int dmabuf_heap_open()
{
	if (dma_buf_opened)
	{
		return dma_heap_fd;
	}
	static const char *heap_name = "/dev/dma_heap/cma@0";

	int fd = open(heap_name, O_RDWR, 0);
	if (fd >= 0) {
		printf("%s: opened %s, fd=%d\n", __func__, heap_name, fd);
		dma_heap_fd = fd;
		dma_buf_opened = 1;
		return fd;
	}
	printf("%s: failed to open %s\n", __func__, heap_name);
	return -1;
}

void dmabuf_heap_close(int heap_fd)
{
	if (!dma_buf_opened) {
		return;
	}
	dma_buf_opened = 0;
	dma_heap_fd = -1;
	close(heap_fd);
}

int dmabuf_heap_alloc(int heap_fd, const char *name, size_t size)
{
	struct dma_heap_allocation_data alloc = { 0 };

	alloc.len = size;
	alloc.fd_flags = O_CLOEXEC | O_RDWR;

	if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc) < 0)
		return -1;

	if (name)
		ioctl(alloc.fd, DMA_BUF_SET_NAME, name);

	return alloc.fd;
}

void* dmabuf_mmap(int fd, size_t size)
{
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap failed");
        return NULL;
    }
    return ptr;
}

void dmabuf_munmap(void *ptr, size_t size)
{
    if (munmap(ptr, size) < 0) {
        perror("munmap failed");
    }
}

static int dmabuf_sync(int buf_fd, bool start)
{
	struct dma_buf_sync sync = { 0 };

	sync.flags = (start ? DMA_BUF_SYNC_START : DMA_BUF_SYNC_END) |
		     DMA_BUF_SYNC_RW;

	do {
		if (ioctl(buf_fd, DMA_BUF_IOCTL_SYNC, &sync) == 0)
			return 0;
	} while ((errno == EINTR) || (errno == EAGAIN));

	return -1;
}

int dmabuf_sync_start(int buf_fd)
{
	return dmabuf_sync(buf_fd, true);
}

int dmabuf_sync_stop(int buf_fd)
{
	return dmabuf_sync(buf_fd, false);
}
