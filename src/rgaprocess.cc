#include "rgaprocess.h"
#include "RgaUtils.h"
#include "gst/gstmemory.h"
#include "gst/gstbufferpool.h"
#include "gst/gststructure.h"
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include "gst/allocators/gstdmabuf.h"
#include "gst/video/video-info.h"
#include "gst/gstinfo.h"


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

    // // 首先检查 buffer 的 pool 信息
    // GstBufferPool *buffer_pool = gst_buf->pool;
    // GST_INFO("Buffer Pool 信息:");
    // if (buffer_pool == NULL) {
    //   GST_INFO("  - Buffer Pool: NULL (缓冲池为空)");
    // } else {
    //   GST_INFO("  - Buffer Pool: %p (非空)", buffer_pool);
    //   GST_INFO("  - Buffer Pool 类型: %s", G_OBJECT_TYPE_NAME(buffer_pool));
      
    //   // 尝试获取缓冲池的配置信息
    //   GstStructure *config = gst_buffer_pool_get_config(buffer_pool);
    //   if (config) {
    //     GST_INFO("  - Buffer Pool 配置: %s", gst_structure_to_string(config));
    //     gst_structure_free(config);
    //   } else {
    //     GST_INFO("  - Buffer Pool 配置: 无法获取");
    //   }
    // }

    GstVideoMeta* meta = gst_buffer_get_video_meta(gst_buf);

    GstVideoFormat format = meta->format;
    gint width = meta->width;
    gint height = meta->height;
    const GstVideoFormatInfo* format_info = gst_video_format_get_info(format);
    gint wstride = meta->stride[0] / format_info->pixel_stride[0];

    //gint hstride = height + alignment.padding_bottom;
    gint hstride = height;
    
    // GST_DEBUG("stride: %d %d %d %d", meta->stride[0], meta->stride[1], meta->stride[2], meta->stride[3]);
    // GST_DEBUG("offset: %zu %zu %zu %zu", meta->offset[0], meta->offset[1], meta->offset[2], meta->offset[3]);
    RgaSURF_FORMAT rga_format = gst_to_rga_format(format);
    if (rga_format == RK_FORMAT_UNKNOWN) {
        return -1;
    }

    // Assuming height is hstride.
    *rga_buf = wrapbuffer_fd_t(fd, width, height, wstride, hstride, rga_format);
    GST_LOG("Wrapping: fd:%d width:%d height:%d wstride:%d hstride:%d format:%d", fd, width, height, wstride, hstride, (int)rga_format);
    return 0;
}

int convert_format(GstObject* obj, GstBuffer* src_buf, GstBuffer* dst_buf)
{
    if (!src_buf || !dst_buf) {
        GST_WARNING_OBJECT(obj, "Invalid buffer parameters in convert_format");
        return -1;
    }

    // // Log source buffer information
    // log_buffer_info(obj, src_buf);
    
    // // Log destination buffer information
    // log_buffer_info(obj, dst_buf);

    rga_buffer_t rga_src_buf;
    rga_buffer_t rga_dst_buf;

    GST_LOG_OBJECT(obj, "Converting buffer format using RGA");

    // Convert GstBuffer to rga_buffer_t
    if (gst_buffer_to_rga_buffer(src_buf, &rga_src_buf) != 0) {
        GST_WARNING_OBJECT(obj, "Failed to convert source buffer to RGA buffer");
        return -1;
    }

    if (gst_buffer_to_rga_buffer(dst_buf, &rga_dst_buf) != 0) {
        GST_WARNING_OBJECT(obj, "Failed to convert destination buffer to RGA buffer");
        return -1;
    }

    // Get dimensions from rga_buffer_t
    int width = rga_src_buf.width;
    int height = rga_src_buf.height;

    GST_DEBUG_OBJECT(obj, "Source dimensions: %dx%d", width, height);

    im_rect src_rect = {0, 0, width, height};
    im_rect dst_rect = {0, 0, width, height};
    rga_buffer_t pat = wrapbuffer_virtualaddr_t(NULL, 0, 0, 0, 0, RK_FORMAT_RGB_888); // pattern unused
    im_rect rect_pat = { 0, 0, 0, 0 }; // pattern rect unused
    // Use RGA to perform the format conversion

    GstMapInfo src_map, dst_map;
    if (!gst_buffer_map(src_buf, &src_map, GST_MAP_READ)) {
        GST_WARNING_OBJECT(obj, "Failed to map source buffer");
        return -1;
    }
    if (!gst_buffer_map(dst_buf, &dst_map, GST_MAP_WRITE)) {
        GST_WARNING_OBJECT(obj, "Failed to map target buffer");
        return -1;
    }

    GST_LOG_OBJECT(obj, "Performing RGA format conversion");
    int ret = improcess(rga_src_buf, rga_dst_buf, pat, src_rect, dst_rect, rect_pat, IM_SYNC);
    if (ret != IM_STATUS_SUCCESS) {
        GST_ERROR_OBJECT(obj, "RGA format conversion failed with error code: %d", ret);
        return -1;
    }

    gst_buffer_unmap(src_buf, &src_map);
    gst_buffer_unmap(dst_buf, &dst_map);
    GST_LOG_OBJECT(obj, "Format conversion completed successfully");
    return 0;
}

int scale_with_aspect_ratio(GstObject* obj, GstBuffer* src_buf, GstBuffer* dst_buf)
{
    if (!src_buf || !dst_buf) {
        GST_WARNING_OBJECT(obj, "Invalid buffer parameters in scale_with_aspect_ratio");
        return -1;
    }
    // Log source buffer information
    // log_buffer_info(obj, src_buf);
    
    // Log destination buffer information
    // log_buffer_info(obj, dst_buf);

    rga_buffer_t rga_src_buf;
    rga_buffer_t rga_dst_buf;

    GST_LOG_OBJECT(obj, "Scaling buffer with aspect ratio using RGA");

    // Convert GstBuffer to rga_buffer_t
    if (gst_buffer_to_rga_buffer(src_buf, &rga_src_buf) != 0) {
        GST_WARNING_OBJECT(obj, "Failed to convert source buffer to RGA buffer");
        return -1;
    }

    if (gst_buffer_to_rga_buffer(dst_buf, &rga_dst_buf) != 0) {
        GST_WARNING_OBJECT(obj, "Failed to convert destination buffer to RGA buffer");
        return -1;
    }

    // Get dimensions from rga_buffer_t
    int src_width = rga_src_buf.width;
    int src_height = rga_src_buf.height;
    int dst_width = rga_dst_buf.width;
    int dst_height = rga_dst_buf.height;

    GST_DEBUG_OBJECT(obj, "Source dimensions: %dx%d, Destination dimensions: %dx%d", 
                     src_width, src_height, dst_width, dst_height);

    // Calculate scaling ratio to maintain aspect ratio
    float scale_w = (float)dst_width / src_width;
    float scale_h = (float)dst_height / src_height;
    float scale = scale_w < scale_h ? scale_w : scale_h;
    
    int new_w = (int)(src_width * scale + 0.5f);
    int new_h = (int)(src_height * scale + 0.5f);
    
    int offset_x = (dst_width - new_w) / 2;
    int offset_y = (dst_height - new_h) / 2;

    GST_DEBUG_OBJECT(obj, "Scaling parameters - Scale: %.2f, New dimensions: %dx%d, Offset: (%d, %d)", 
                     scale, new_w, new_h, offset_x, offset_y);

    im_rect src_rect = {0, 0, src_width, src_height};
    im_rect dst_rect = {offset_x, offset_y, new_w, new_h};
    
    // Use RGA to perform the scaling with aspect ratio
    GST_LOG_OBJECT(obj, "Performing RGA scaling with aspect ratio");
    int ret = improcess(rga_src_buf, rga_dst_buf, {}, src_rect, dst_rect, {}, IM_SYNC);
    if (ret != IM_STATUS_SUCCESS) {
        GST_ERROR_OBJECT(obj, "RGA scaling with aspect ratio failed with error code: %d", ret);
        return -1;
    }

    GST_LOG_OBJECT(obj, "Scaling with aspect ratio completed successfully");
    return 0;
}

#ifdef __cplusplus
}
#endif
