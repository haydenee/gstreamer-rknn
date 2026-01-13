#include "rgaprocess.h"
#include <cmath>
#include <cstdio>
#include <gst/allocators/gstdmabuf.h>
#include <gst/gstbuffer.h>
#include <gst/video/video.h>
#include <gst/gst.h>
#include <im2d.hpp>
#include <mutex>
#include <unordered_map>

GST_DEBUG_CATEGORY_EXTERN(gst_plugin_rknn_debug);
#define GST_CAT_DEFAULT gst_plugin_rknn_debug

#define GST_RGA_FORMAT(gst, rga, pixel_stride0, yuv)                           \
  {GST_VIDEO_FORMAT_##gst, RK_FORMAT_##rga, pixel_stride0, yuv}

struct gst_rga_format {
  GstVideoFormat gst_format;
  RgaSURF_FORMAT rga_format;
  int pixel_stride0;
  int yuv;
};

static struct gst_rga_format gst_rga_formats[] = {
    GST_RGA_FORMAT(I420, YCbCr_420_P, 1, 1),
    GST_RGA_FORMAT(YV12, YCrCb_420_P, 1, 1),
    GST_RGA_FORMAT(NV12, YCbCr_420_SP, 1, 1),
    GST_RGA_FORMAT(NV21, YCrCb_420_SP, 1, 1),
    GST_RGA_FORMAT(Y42B, YCbCr_422_P, 1, 1),
    GST_RGA_FORMAT(NV16, YCbCr_422_SP, 1, 1),
    GST_RGA_FORMAT(NV61, YCrCb_422_SP, 1, 1),
    GST_RGA_FORMAT(BGR16, RGB_565, 2, 0),
    GST_RGA_FORMAT(RGB, RGB_888, 3, 0),
    GST_RGA_FORMAT(BGR, BGR_888, 3, 0),
    GST_RGA_FORMAT(RGBA, RGBA_8888, 4, 0),
    GST_RGA_FORMAT(BGRA, BGRA_8888, 4, 0),
    GST_RGA_FORMAT(RGBx, RGBX_8888, 4, 0),
    GST_RGA_FORMAT(BGRx, BGRX_8888, 4, 0),
};

static std::unordered_map<int, rga_buffer_t> fd_map;
static std::mutex fd_map_mutex;
RgaSURF_FORMAT gst_to_rga_format(GstVideoFormat gst_format) {
  for (unsigned int i = 0;
       i < sizeof(gst_rga_formats) / sizeof(gst_rga_formats[0]); i++) {
    if (gst_rga_formats[i].gst_format == gst_format)
      return gst_rga_formats[i].rga_format;
  }
  return RK_FORMAT_UNKNOWN;
}

GstVideoFormat rga_to_gst_format(RgaSURF_FORMAT rga_format) {
  for (unsigned int i = 0;
       i < sizeof(gst_rga_formats) / sizeof(gst_rga_formats[0]); i++) {
    if (gst_rga_formats[i].rga_format == rga_format)
      return gst_rga_formats[i].gst_format;
  }
  return GST_VIDEO_FORMAT_UNKNOWN;
}

gboolean save_rgb_to_bmp(const char *filename, const unsigned char *rgb_data,
                         int width, int height) {
  FILE *fout = fopen(filename, "wb");
  if (!fout) {
    return FALSE;
  }

  int row_stride = width * 3;
  int pad = (4 - (row_stride % 4)) % 4;
  int bmp_data_size = (row_stride + pad) * height;
  int file_size = 54 + bmp_data_size;
  GST_DEBUG("stride %d pad %d file_size %d", row_stride, pad, file_size);
  // BMP file header (14 bytes)
  unsigned char bmp_file_header[14] = {
      'B',
      'M',
      (unsigned char)(file_size & 0xFF),
      (unsigned char)((file_size >> 8) & 0xFF),
      (unsigned char)((file_size >> 16) & 0xFF),
      (unsigned char)((file_size >> 24) & 0xFF),
      0,
      0,
      0,
      0,
      54,
      0,
      0,
      0};

  // BMP info header (40 bytes)
  unsigned char bmp_info_header[40] = {
      40,
      0,
      0,
      0,
      (unsigned char)(width & 0xFF),
      (unsigned char)((width >> 8) & 0xFF),
      (unsigned char)((width >> 16) & 0xFF),
      (unsigned char)((width >> 24) & 0xFF),
      (unsigned char)(height & 0xFF),
      (unsigned char)((height >> 8) & 0xFF),
      (unsigned char)((height >> 16) & 0xFF),
      (unsigned char)((height >> 24) & 0xFF),
      1,
      0,
      24,
      0,
      0,
      0,
      0,
      0,
      (unsigned char)(bmp_data_size & 0xFF),
      (unsigned char)((bmp_data_size >> 8) & 0xFF),
      (unsigned char)((bmp_data_size >> 16) & 0xFF),
      (unsigned char)((bmp_data_size >> 24) & 0xFF),
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0};

  fwrite(bmp_file_header, 1, 14, fout);
  fwrite(bmp_info_header, 1, 40, fout);

  unsigned char pad_bytes[3] = {0, 0, 0};
  for (int y = height - 1; y >= 0; y--) {
    const unsigned char *row = rgb_data + y * width * 3;
    for (int x = 0; x < width; x++) {
      unsigned char bgr[3] = {row[x * 3 + 2], row[x * 3 + 1], row[x * 3 + 0]};
      fwrite(bgr, 1, 3, fout);
    }
    fwrite(pad_bytes, 1, pad, fout);
  }
  fclose(fout);
  return TRUE;
}
int calc_buffer_size(int width, int height, GstVideoFormat gst_format) {
  for (unsigned int i = 0;
       i < sizeof(gst_rga_formats) / sizeof(gst_rga_formats[0]); i++) {
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

rga_buffer_t gst_buffer_to_rga_buffer(GstBuffer *gst_buf) {
  rga_buffer_t ret;
  if (!gst_buf) {
    GST_ERROR("invalid buffer: gst_buf %p", gst_buf);
    return ret;
  }

  GstMemory *mem = gst_buffer_peek_memory(gst_buf, 0);
  if (!mem || !gst_is_dmabuf_memory(mem)) {
    GST_ERROR("invalid memory: mem %p", mem);
    return ret;
  }

  int fd = gst_dmabuf_memory_get_fd(mem);
  if (fd < 0) {
    GST_ERROR("invalid dmabuf fd: %d", fd);
    return ret;
  }

  GstVideoMeta *meta = gst_buffer_get_video_meta(gst_buf);
  GstVideoFormat format = meta->format;
  gint width = meta->width;
  gint height = meta->height;
  const GstVideoFormatInfo *format_info = gst_video_format_get_info(format);

  gint wstride = meta->stride[0] / format_info->pixel_stride[0];
  // 注意，mpp 有一个 bug，它返回的 NV12/NV16 格式的 buffer，UV 平面的 offset
  // 是错的，实际对齐到了 16 像素但给的 offset 对齐到了 2。 加了个选项
  // mppjpegdec_offset_workaround 修了。
  gint hstride =
      meta->n_planes > 1 ? meta->offset[1] / meta->stride[0] : height;

  GST_TRACE("stride: %d %d %d %d", meta->stride[0], meta->stride[1],
            meta->stride[2], meta->stride[3]);
  GST_TRACE("offset: %zu %zu %zu %zu", meta->offset[0], meta->offset[1],
            meta->offset[2], meta->offset[3]);
  RgaSURF_FORMAT rga_format = gst_to_rga_format(format);

  if (rga_format == RK_FORMAT_UNKNOWN) {
    return ret;
  }

  std::lock_guard<std::mutex> lock(fd_map_mutex);
  // Assuming height is hstride.
  auto it = fd_map.find(fd);
  if (it == fd_map.end()) {
    GstMemory *mem = gst_buffer_peek_memory(gst_buf, 0);
    size_t mem_size = mem->maxsize;
    auto handle = importbuffer_fd(fd, mem_size);
    GST_DEBUG("importbuffer_fd %d %zu get handle %d", fd, mem_size, handle);
    rga_buffer_t mapped =
        wrapbuffer_handle(handle, width, height, rga_format, wstride, hstride);
    it = fd_map.emplace(fd, mapped).first;
    GST_DEBUG(
        "Wrapping: fd:%d width:%d height:%d wstride:%d hstride:%d format:%d",
        fd, width, height, wstride, hstride, (int)rga_format);
  }
  GST_DEBUG("fd %d already mapped", fd);
  return it->second;
}
