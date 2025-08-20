#ifndef _RGA_PROCESS_H_
#define _RGA_PROCESS_H_

#include "gst/video/video-format.h"
#include "gst/gstbuffer.h"
#include "rga.h"
#include "rknnprocess.h"
#ifdef __cplusplus
extern "C" {
#endif


RgaSURF_FORMAT gst_to_rga_format(GstVideoFormat gst_format);
GstVideoFormat rga_to_gst_format(RgaSURF_FORMAT rga_format);
gboolean save_rgb_to_bmp(const char* filename, const unsigned char* rgb_data, int width, int height);
int calc_buffer_size(int width, int height, GstVideoFormat gst_format);
// RGA buffer processing functions
rga_buffer_t gst_buffer_to_rga_buffer(GstBuffer* gst_buf);
int raw_to_rknn(struct RknnEngine* obj, GstBuffer* src_buf, GstBuffer* dst_buf);
#ifdef __cplusplus
}
#endif

#endif