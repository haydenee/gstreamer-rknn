#include <gst/gst.h>
#include "../src/dmabuf.h"
#include <stdio.h>
#include <stdlib.h>

// 定义调试类别，解决链接错误
GST_DEBUG_CATEGORY(gst_plugin_rknn_debug);
#define GST_CAT_DEFAULT gst_plugin_rknn_debug

int main(int argc, char *argv[]) {
    GstBuffer *buffer;
    size_t mem_count = 3;
    size_t sizes[] = {1024, 2048, 4096};
    
    // 初始化 GStreamer
    gst_init(&argc, &argv);
    
    // 初始化调试类别
    GST_DEBUG_CATEGORY_INIT(gst_plugin_rknn_debug, "rknn", 0, "RKNN plugin");
    
    // 测试 dma_buffer_new 函数
    buffer = dma_buffer_new(mem_count, sizes);
    if (!buffer) {
        printf("Failed to create DMA buffer\n");
        return -1;
    }
    
    printf("Successfully created DMA buffer with %zu memory blocks\n", mem_count);
    
    // 打印 buffer 信息
    log_buffer_info(buffer);
    
    // 释放 buffer
    gst_buffer_unref(buffer);
    
    // 关闭 DMA heap
    dmabuf_heap_close();
    
    printf("Test completed successfully\n");
    return 0;
}