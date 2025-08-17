#include "rgaprocess.h"
#include "RgaUtils.h"
#include "dmabuf.h"
#include "gst/gstmemory.h"
#include "gst/gstbufferpool.h"
#include "gst/gststructure.h"
#include "im2d_type.h"
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include "gst/allocators/gstdmabuf.h"
#include "gst/video/video-info.h"
#include "gst/gstinfo.h"
#include "gstrknn.h"
#include <math.h>


GST_DEBUG_CATEGORY_EXTERN (gst_plugin_rknn_debug);
#define GST_CAT_DEFAULT gst_plugin_rknn_debug

#define GST_RGA_FORMAT(gst, rga, pixel_stride0, yuv) \
  { GST_VIDEO_FORMAT_ ## gst, RK_FORMAT_ ## rga, pixel_stride0, yuv }

struct gst_rga_format {
  GstVideoFormat gst_format;
  RgaSURF_FORMAT rga_format;
  int pixel_stride0;
  int yuv;
};

static struct gst_rga_format gst_rga_formats[] = {
  GST_RGA_FORMAT (I420, YCbCr_420_P, 1, 1),
  GST_RGA_FORMAT (YV12, YCrCb_420_P, 1, 1),
  GST_RGA_FORMAT (NV12, YCbCr_420_SP, 1, 1),
  GST_RGA_FORMAT (NV21, YCrCb_420_SP, 1, 1),
  GST_RGA_FORMAT (Y42B, YCbCr_422_P, 1, 1),
  GST_RGA_FORMAT (NV16, YCbCr_422_SP, 1, 1),
  GST_RGA_FORMAT (NV61, YCrCb_422_SP, 1, 1),
  GST_RGA_FORMAT (BGR16, RGB_565, 2, 0),
  GST_RGA_FORMAT (RGB, RGB_888, 3, 0),
  GST_RGA_FORMAT (BGR, BGR_888, 3, 0),
  GST_RGA_FORMAT (RGBA, RGBA_8888, 4, 0),
  GST_RGA_FORMAT (BGRA, BGRA_8888, 4, 0),
  GST_RGA_FORMAT (RGBx, RGBX_8888, 4, 0),
  GST_RGA_FORMAT (BGRx, BGRX_8888, 4, 0),
};

#ifdef __cplusplus
extern "C" {
#endif

RgaSURF_FORMAT gst_to_rga_format (GstVideoFormat gst_format)
{
  for (unsigned int i = 0; i < sizeof(gst_rga_formats)/sizeof(gst_rga_formats[0]); i++) {
    if (gst_rga_formats[i].gst_format == gst_format)
      return gst_rga_formats[i].rga_format;
  }
  return RK_FORMAT_UNKNOWN;
}

GstVideoFormat rga_to_gst_format(RgaSURF_FORMAT rga_format)
{
  for (unsigned int i = 0; i < sizeof(gst_rga_formats)/sizeof(gst_rga_formats[0]); i++) {
    if (gst_rga_formats[i].rga_format == rga_format)
      return gst_rga_formats[i].gst_format;
  }
  return GST_VIDEO_FORMAT_UNKNOWN;
}

gboolean save_rgb_to_bmp(const char* filename, const unsigned char* rgb_data, int width, int height)
{
    FILE* fout = fopen(filename, "wb");
    if (!fout) {
        return FALSE;
    }

    int row_stride = width * 3;
    int pad = (4 - (row_stride % 4)) % 4;
    int bmp_data_size = (row_stride + pad) * height;
    int file_size = 54 + bmp_data_size;
    GST_DEBUG("stride %d pad %d file_size %d", row_stride, pad ,file_size);
    // BMP file header (14 bytes)
    unsigned char bmp_file_header[14] = {
        'B', 'M',
        (unsigned char)(file_size & 0xFF),
        (unsigned char)((file_size >> 8) & 0xFF),
        (unsigned char)((file_size >> 16) & 0xFF),
        (unsigned char)((file_size >> 24) & 0xFF),
        0, 0, 0, 0,
        54, 0, 0, 0
    };

    // BMP info header (40 bytes)
    unsigned char bmp_info_header[40] = {
        40, 0, 0, 0,
        (unsigned char)(width & 0xFF),
        (unsigned char)((width >> 8) & 0xFF),
        (unsigned char)((width >> 16) & 0xFF),
        (unsigned char)((width >> 24) & 0xFF),
        (unsigned char)(height & 0xFF),
        (unsigned char)((height >> 8) & 0xFF),
        (unsigned char)((height >> 16) & 0xFF),
        (unsigned char)((height >> 24) & 0xFF),
        1, 0,
        24, 0,
        0, 0, 0, 0,
        (unsigned char)(bmp_data_size & 0xFF),
        (unsigned char)((bmp_data_size >> 8) & 0xFF),
        (unsigned char)((bmp_data_size >> 16) & 0xFF),
        (unsigned char)((bmp_data_size >> 24) & 0xFF),
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    };

    fwrite(bmp_file_header, 1, 14, fout);
    fwrite(bmp_info_header, 1, 40, fout);

    unsigned char pad_bytes[3] = { 0, 0, 0 };
    for (int y = height - 1; y >= 0; y--) {
        const unsigned char* row = rgb_data + y * width * 3;
        for (int x = 0; x < width; x++) {
            unsigned char bgr[3] = { row[x * 3 + 2], row[x * 3 + 1], row[x * 3 + 0] };
            fwrite(bgr, 1, 3, fout);
        }
        fwrite(pad_bytes, 1, pad, fout);
    }
    fclose(fout);
    return TRUE;
}
int calc_buffer_size(int width, int height, GstVideoFormat gst_format)
{
    for (unsigned int i = 0; i < sizeof(gst_rga_formats)/sizeof(gst_rga_formats[0]); i++) {
        if (gst_rga_formats[i].gst_format == gst_format) {
            if (gst_rga_formats[i].yuv) {
                // 针对不同YUV格式分别计算
                switch (gst_format) {
                    case GST_VIDEO_FORMAT_I420:
                    case GST_VIDEO_FORMAT_YV12:
                    case GST_VIDEO_FORMAT_NV12:
                    case GST_VIDEO_FORMAT_NV21:
                        // YUV 4:2:0
                        return width * height * 3 / 2;
                    case GST_VIDEO_FORMAT_Y42B:
                        // YUV 4:2:2 planar
                        return width * height * 2;
                    case GST_VIDEO_FORMAT_NV16:
                    case GST_VIDEO_FORMAT_NV61:
                        // YUV 4:2:2 semi-planar
                        return width * height * 2;
                    default:
                        // 其他YUV格式可根据实际需求补充
                        return 0;
                }
            } else {
                // RGB/BGR等格式
                return width * height * gst_rga_formats[i].pixel_stride0;
            }
        }
    }
    // 未知格式
    return 0;
}

int gst_buffer_to_rga_buffer(GstBuffer* gst_buf, rga_buffer_t* rga_buf)
{
    if (!gst_buf || !rga_buf) {
        return -1;
    }

    GstMemory* mem = gst_buffer_peek_memory(gst_buf, 0);
    if (!mem || !gst_is_dmabuf_memory(mem)) {
        return -1;
    }

    int fd = gst_dmabuf_memory_get_fd(mem);
    if (fd < 0) {
        return -1;
    }

    GstVideoMeta* meta = gst_buffer_get_video_meta(gst_buf);

    GstVideoFormat format = meta->format;
    gint width = meta->width;
    gint height = meta->height;
    const GstVideoFormatInfo* format_info = gst_video_format_get_info(format);
    gint wstride = meta->stride[0] / format_info->pixel_stride[0]; 
    // 注意，mpp 有一个 bug，它返回的 NV12/NV16 格式的 buffer，UV 平面的 offset 是错的，实际对齐到了 16 像素但给的 offset 对齐到了 2。
    // 加了个选项 mppjpegdec_offset_workaround 修了。
    gint hstride = meta->n_planes > 1 ? meta->offset[1] / meta->stride[0] : height;

    GST_TRACE("stride: %d %d %d %d", meta->stride[0], meta->stride[1], meta->stride[2], meta->stride[3]);
    GST_TRACE("offset: %zu %zu %zu %zu", meta->offset[0], meta->offset[1], meta->offset[2], meta->offset[3]);
    RgaSURF_FORMAT rga_format = gst_to_rga_format(format);
    if (rga_format == RK_FORMAT_UNKNOWN) {
        return -1;
    }
    
    // Assuming height is hstride.
    *rga_buf = wrapbuffer_fd_t(fd, width, height, wstride, hstride, rga_format);
    GST_DEBUG("Wrapping: fd:%d width:%d height:%d wstride:%d hstride:%d format:%d", fd, width, height, wstride, hstride, (int)rga_format);
    return 0;
}

int test_draw_rectangle(GstBuffer* src_buf) {
    rga_buffer_t rga_buf;
    gst_buffer_to_rga_buffer(src_buf, &rga_buf);
    im_rect rect;
    rect.width = 100;
    rect.height = 100;
    rect.x = 100;
    rect.y = 100;
    int ret = imcheck({}, rga_buf, {}, rect, IM_COLOR_FILL);
    GST_DEBUG("imcheck ret %d", ret);
    ret = imrectangle(rga_buf, rect, 0x80808080, 10);

    GST_DEBUG("test imrectangle ret %d", ret);
    return 0;
}

int raw_to_rknn(struct RknnEngine* engine, GstBuffer* src_buf, GstBuffer* dst_buf)
{
    if (!src_buf || !dst_buf) {
        GST_WARNING("Invalid buffer parameters in convert_format");
        return -1;
    }
    GST_DEBUG("convert buf info:");
    log_buffer_info(src_buf);
    log_buffer_info(dst_buf);
    // // Log source buffer information
    // log_buffer_info(obj, src_buf);
    
    // // Log destination buffer information
    // log_buffer_info(obj, dst_buf);

    rga_buffer_t rga_src_buf;
    rga_buffer_t rga_dst_buf;

    GST_DEBUG("Converting buffer format using RGA");

    // Convert GstBuffer to rga_buffer_t
    if (gst_buffer_to_rga_buffer(src_buf, &rga_src_buf) != 0) {
        GST_WARNING("Failed to convert source buffer to RGA buffer");
        return -1;
    }
    if (gst_buffer_to_rga_buffer(dst_buf, &rga_dst_buf) != 0) {
        GST_WARNING("Failed to convert destination buffer to RGA buffer");
        return -1;
    }

    // Get dimensions from rga_buffer_t
    int src_offset_x = engine->img_pad_left;
    int src_offset_y = engine->img_pad_top;
    int dst_offset_x = engine->model_pad_left;
    int dst_offset_y = engine->model_pad_top;
    int src_width = engine->img_width - engine->img_pad_left - engine->img_pad_right;
    int src_height = engine->img_height - engine->img_pad_top - engine->img_pad_bottom;
    int dst_width = engine->model_width - engine->model_pad_left - engine->model_pad_right;
    int dst_height = engine->model_height - engine->model_pad_top - engine->model_pad_bottom;
    GST_DEBUG("Source dimensions: %dx%d Destination dimensions %dx%d ", src_width, src_height, dst_width, dst_height);

    im_rect src_rect = {src_offset_x, src_offset_y, src_width, src_height};
    im_rect dst_rect = {dst_offset_x, dst_offset_y, dst_width, dst_height};
    GST_DEBUG("Source rectangle: %d,%d,%d,%d Destination rectangle: %d,%d,%d,%d", src_rect.x, src_rect.y, src_rect.width, src_rect.height, dst_rect.x, dst_rect.y, dst_rect.width, dst_rect.height);
    // Use RGA to perform the format conversion

    GstMapInfo src_map, dst_map;
    if (!gst_buffer_map(src_buf, &src_map, GST_MAP_READ)) {
        GST_WARNING("Failed to map source buffer");
        return -1;
    }
    if (!gst_buffer_map(dst_buf, &dst_map, GST_MAP_WRITE)) {
        GST_WARNING("Failed to map target buffer");
        return -1;
    }
    
    GST_DEBUG("Performing RGA format conversion");
    int ret = improcess(rga_src_buf, rga_dst_buf, {}, src_rect, dst_rect, {}, IM_SYNC);
    if (ret != IM_STATUS_SUCCESS) {
        GST_ERROR("RGA format conversion failed with error code: %d", ret);
        return -1;
    }
    gst_buffer_unmap(src_buf, &src_map);
    gst_buffer_unmap(dst_buf, &dst_map);
    GST_DEBUG("Format conversion completed successfully");
    return 0;
}

#ifdef __cplusplus
}
#endif
