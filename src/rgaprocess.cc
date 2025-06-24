#include "rgaprocess.h"
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"


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
        file_size, file_size >> 8, file_size >> 16, file_size >> 24,
        0, 0, 0, 0,
        54, 0, 0, 0
    };

    // BMP info header (40 bytes)
    unsigned char bmp_info_header[40] = {
        40, 0, 0, 0,
        width, width >> 8, width >> 16, width >> 24,
        height, height >> 8, height >> 16, height >> 24,
        1, 0,
        24, 0,
        0, 0, 0, 0,
        bmp_data_size, bmp_data_size >> 8, bmp_data_size >> 16, bmp_data_size >> 24,
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
#ifdef __cplusplus
}
#endif
