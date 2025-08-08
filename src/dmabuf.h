/*
 * GStreamer
 * Copyright (C) 2025 Your Name <your-email@example.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __DMA_BUF_H__
#define __DMA_BUF_H__

#include <gst/gst.h>
#include <gst/video/gstvideopool.h>

G_BEGIN_DECLS

// Buffer pool and buffer management functions
gboolean dmabuf_heap_open(void);
gint dmabuf_heap_alloc(gsize size);
void dmabuf_heap_close(void);

// Buffer logging function
void log_buffer_info(GstBuffer* buf);

// Buffer pool creation function
GstVideoBufferPool* dma_buffer_pool_new(GstVideoFormat format, gint width, gint height, gint w_align, gint h_align);

G_END_DECLS

#endif /* __DMA_BUF_H__ */