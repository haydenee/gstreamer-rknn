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

#include <gst/gstallocator.h>
#include <gst/gstbuffer.h>
#include <gst/gstbufferpool.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include <gst/video/video.h>
#include <stddef.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "dmabuf.h"
#include <gst/allocators/gstdmabuf.h>
#include <gst/gst.h>
#include <gst/gstmemory.h>
#include <gst/video/video-format.h>
#include <gst/video/video-info.h>

#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

GST_DEBUG_CATEGORY_EXTERN(gst_plugin_rknn_debug);
#define GST_CAT_DEFAULT gst_plugin_rknn_debug

// 全局 DMA heap 文件描述符
static int dma_heap_fd = -1;
static gboolean dma_heap_opened = FALSE;

// 打开 DMA heap
gboolean dmabuf_heap_open(void) {
  if (dma_heap_opened) {
    return dma_heap_fd > 0;
  }

  static const char *heap_name = "/dev/dma_heap/cma@0";
  int fd = open(heap_name, O_RDWR, 0);
  if (fd >= 0) {
    dma_heap_fd = fd;
    dma_heap_opened = TRUE;
    return fd;
  }

  GST_ERROR("Failed to open %s", heap_name);
  return -1;
}

// 从 DMA heap 申请一个 DMA buffer 文件描述符
gint dmabuf_heap_alloc(gsize size) {
  struct dma_heap_allocation_data alloc = {0};

  alloc.len = size;
  alloc.fd_flags = O_CLOEXEC | O_RDWR;

  if (ioctl(dma_heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc) < 0) {
    GST_ERROR("Failed to allocate DMA buffer: %s", g_strerror(errno));
    return -1;
  }

  return alloc.fd;
}

// 关闭 DMA heap
void dmabuf_heap_close(void) {
  if (!dma_heap_opened) {
    return;
  }

  dma_heap_opened = FALSE;
  if (dma_heap_fd >= 0) {
    close(dma_heap_fd);
    dma_heap_fd = -1;
  }
}

// 自定义allocator的alloc函数实现
GstMemory *dmabuf_allocator_alloc(GstAllocator *allocator, gsize size,
                                  GstAllocationParams *params) {
  gint fd;
  GstMemory *mem;

  // 使用我们的dma-heap分配一个fd
  fd = dmabuf_heap_alloc(size);
  if (fd < 0) {
    GST_ERROR("Failed to allocate dma buffer of size %zu", size);
    return NULL;
  }

  // 使用GstDmabufAllocator来创建GstMemory
  mem = gst_dmabuf_allocator_alloc(allocator, fd, size);
  if (!mem) {
    GST_ERROR("Failed to create dmabuf memory from fd %d", fd);
    close(fd); // 如果创建失败，需要手动关闭fd
    return NULL;
  }

  return mem;
}

/**
 * dmabuf_buffer_pool_new:
 * @format: The video format (e.g., GST_VIDEO_FORMAT_NV12,
 * GST_VIDEO_FORMAT_I420).
 * @width: The width of the video frame.
 * @height: The height of the video frame.
 * @align: The alignment requirement (e.g., 16, 32). Both width and height
 *         (and potentially stride/offset) will be aligned to multiples of this
 * value.
 *
 * Creates a new #GstBufferPool configured to allocate DMA-BUF backed video
 * buffers with the specified format, dimensions, and alignment.
 *
 * This function leverages GStreamer's video utilities to calculate buffer sizes
 * and offsets, avoiding manual switch-case logic for different formats.
 *
 * Returns: (transfer full): A new #GstBufferPool, or %NULL on failure.
 *          The caller is responsible for unreffing the pool with
 * gst_object_unref().
 */


GstVideoBufferPool *dma_buffer_pool_new(GstVideoFormat format, gint width,
                                        gint height, gint w_align, gint h_align) {
  GstBufferPool *pool = NULL;
  GstAllocator *allocator = NULL;
  GstStructure *config = NULL;
  GstCaps *caps = NULL;
  GstVideoInfo info = {0};             // 初始化为0
  GstVideoAlignment video_align = {0}; // 初始化为0
  guint min_buffers = 3;               // 示例最小缓冲区数
  guint max_buffers = 6;               // 0 表示无上限
  gsize buffer_size;

  // 确保 dma heap 已打开
  if (!dmabuf_heap_open()) {
    GST_ERROR("Failed to open dmabuf heap.\n");
    goto error;
  }
  // 创建 GstVideoBufferPool
  pool = gst_video_buffer_pool_new();
  if (!pool) {
    GST_ERROR("Failed to create GstVideoBufferPool.\n");
    goto error;
  }
  // 创建 GstCaps 并用 GstCaps 创建 GstVideoInfo
  caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING,
                             gst_video_format_to_string(format), "width",
                             G_TYPE_INT, width, "height", G_TYPE_INT, height,
                             NULL);

  if (!caps) {
    GST_ERROR("Failed to create caps.\n");
    goto error;
  }
  // 从 GstCaps 获取 GstVideoInfo
  if (!gst_video_info_from_caps(&info, caps)) {
    GST_ERROR("Failed to parse caps into GstVideoInfo.\n");
    goto error;
  }
  // 配置缓冲池
  config = gst_buffer_pool_get_config(pool);
  if (!config) {
    GST_ERROR("Failed to get buffer pool config.\n");
    goto error;
  }
  // 启用视频对齐选项
  gst_buffer_pool_config_add_option(config,
                                    GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  // 推荐启用 GstVideoMeta 以便访问实际的 stride/offset 信息
  gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  // --- 设置 GstVideoAlignment ---
  // 根据 align 要求设置 GstVideoAlignment 结构。
  // padding_right 确保最后一列像素后的内存对齐。
  // padding_bottom 确保最后一行像素后的内存对齐。
  // stride_align 可以确保每一行数据的起始地址对齐（如果需要）。
  // 这里假设 align 主要用于 width/height 对齐，并可能影响 stride。
  // GStreamer 会根据 info 和 video_align 计算最终的 buffer_size 和 offsets。
  video_align.padding_right = GST_ROUND_UP_N(width, w_align) - width;
  video_align.padding_bottom = GST_ROUND_UP_N(height, h_align) - height;
  // 如果需要每一行的 stride 也对齐，可以设置：
  // 注意：stride_align 是一个掩码，通常设置为 (align - 1)。
  for (int i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    video_align.stride_align[i] = w_align - 1; // align-1 because it's a mask
  }

  // 将对齐信息应用到配置
  gst_buffer_pool_config_set_video_alignment(config, &video_align);

  // --- 设置分配器 ---
  allocator = gst_dmabuf_allocator_new();
  if (!allocator) {
    GST_ERROR("Failed to create GstDmaBufAllocator.\n");
    goto error;
  }

  // 用通过 dma heap 分配 fd 的覆盖默认行为
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_GET_CLASS(allocator);
  allocator_class->alloc = dmabuf_allocator_alloc;

  // GstAllocationParams alloc_params = {0};
  // gst_allocation_params_init(&alloc_params);
  // // 如果需要内存起始地址对齐（例如 4K 页面对齐），可以设置：
  // // alloc_params.align = 4095; // 例如，4K 对齐 (4096 - 1)
  // // 注意：这与视频像素数据的对齐 (GstVideoAlignment) 是不同的概念。
  // GST_LOG("设置分配器参数: align=%" G_GSIZE_FORMAT, alloc_params.align);

  // gst_buffer_pool_config_set_allocator(config, allocator, &alloc_params);
  gst_buffer_pool_config_set_allocator(config, allocator, NULL);

  // --- 设置基本参数 ---
  // 先设置一个初始 size。gst_buffer_pool_set_config 会根据 caps 和 alignment
  // 重新计算。
  buffer_size = info.size; // 初始大小，后续会更新
  gst_buffer_pool_config_set_params(config, caps, buffer_size, min_buffers,
                                    max_buffers);

  // 5. 应用配置
  // gst_buffer_pool_set_config 会根据 caps, allocator, alignment 等重新计算
  // 实际需要的 buffer_size。
  if (!gst_buffer_pool_set_config(pool, config)) {
    GST_ERROR("Failed to set buffer pool configuration.\n");
    goto error;
  }
  // config 已被 pool 接管，不再需要手动释放

  // 6. 激活缓冲池
  if (!gst_buffer_pool_set_active(pool, TRUE)) {
    GST_ERROR("Failed to activate buffer pool.\n");
    goto error;
  }

  // 清理局部引用
  if (caps)
    gst_caps_unref(caps);
  if (allocator)
    gst_object_unref(allocator);

  return GST_VIDEO_BUFFER_POOL(pool);

error:
  if (pool)
    gst_object_unref(pool);
  if (allocator)
    gst_object_unref(allocator);
  if (caps)
    gst_caps_unref(caps);
  if (config)
    gst_structure_free(config); // 如果 config 没有被 set_config 接管

  return NULL;
}

// Buffer logging function
void log_buffer_info(GstBuffer *buf) {
  if (!buf) {
    GST_WARNING("Buffer is NULL");
    return;
  }

  // 获取 buffer 的基本信息
  GstClockTime pts = GST_BUFFER_PTS(buf);
  guint64 offset = GST_BUFFER_OFFSET(buf);
  guint size = gst_buffer_get_size(buf);

  GST_DEBUG("Buffer basic info - PTS: %" GST_TIME_FORMAT
            ", Offset: %" G_GUINT64_FORMAT
            ", Size: %u bytes",
            GST_TIME_ARGS(pts), offset, size);

  // 打印 buffer 中所有的内存块信息
  guint num_mems = gst_buffer_n_memory(buf);
  GST_DEBUG("Buffer contains %u memory blocks:", num_mems);
  
  for (guint i = 0; i < num_mems; i++) {
    GstMemory *mem = gst_buffer_peek_memory(buf, i);
    if (mem) {
      GstMapInfo map_info;
      
      // 尝试映射内存以获取更多信息
      if (gst_memory_map(mem, &map_info, GST_MAP_READ)) {
        GST_DEBUG("  Memory block %u - Size: %" G_GSIZE_FORMAT
                  ", Maxsize: %" G_GSIZE_FORMAT
                  ", Offset: %" G_GSIZE_FORMAT
                  ", Allocator: %s"
                  ", Parent: %p"
                  ", Mapped: yes (size: %" G_GSIZE_FORMAT ")"
                  ", Pointer: %p",
                  i,
                  mem->size,
                  mem->maxsize,
                  mem->offset,
                  mem->allocator ? GST_OBJECT_NAME(mem->allocator) : "none",
                  mem->parent,
                  map_info.size,
                  map_info.data
                );
        
        // 尝试获取 DMA-BUF 特定信息
        if (mem->allocator && g_strcmp0(GST_OBJECT_NAME(mem->allocator), "GstDmaBufAllocator") == 0) {
          gint fd = gst_dmabuf_memory_get_fd(mem);
          GST_DEBUG("    DMA-BUF FD: %d", fd);
        }
        
        gst_memory_unmap(mem, &map_info);
      } else {
        GST_DEBUG("  Memory block %u - Size: %" G_GSIZE_FORMAT
                  ", Maxsize: %" G_GSIZE_FORMAT
                  ", Offset: %" G_GSIZE_FORMAT
                  ", Allocator: %s"
                  ", Parent: %p"
                  ", Mapped: no",
                  i,
                  mem->size,
                  mem->maxsize,
                  mem->offset,
                  mem->allocator ? GST_OBJECT_NAME(mem->allocator) : "none",
                  mem->parent);
      }
    } else {
      GST_DEBUG("  Memory block %u - NULL", i);
    }
  }

  // 尝试从 buffer 获取视频元数据
  GstVideoMeta *meta = gst_buffer_get_video_meta(buf);
  if (meta) {
    // 从 GstVideoMeta 获取 width, height 和 stride 信息
    guint width = meta->width;
    guint height = meta->height;
    guint n_planes = meta->n_planes;
    GstVideoFormat format = meta->format;

    // 打印 buffer 信息，包含格式
    GST_DEBUG("Buffer video meta - Format: %s, Width: %u, Height: %u, Planes: %u",
              gst_video_format_to_string(format), width, height, n_planes);

    // 打印每个平面的 stride 信息
    for (guint i = 0; i < n_planes && i < GST_VIDEO_MAX_PLANES; i++) {
      GST_DEBUG("  Plane %u - Offset: %" G_GSIZE_FORMAT ", Stride: %d", i,
                meta->offset[i], meta->stride[i]);
    }
  }
  
}
