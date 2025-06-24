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

#ifdef __cplusplus
}
#endif


void letterbox(const cv::Mat &src_image, cv::Mat &padded_image, BOX_RECT &pads, const cv::Size &src_size, const cv::Size &target_size, const int src_format, const int target_format, const cv::Scalar &pad_color)
{

    // 调整图像大小
    cv::Mat resized_image;
    int scale;
    cv::resize(src_image, resized_image, cv::Size(), scale, scale);

    // 计算填充大小
    int pad_width = target_size.width - resized_image.cols;
    int pad_height = target_size.height - resized_image.rows;

    pads.left = pad_width / 2;
    pads.right = pad_width - pads.left;
    pads.top = pad_height / 2;
    pads.bottom = pad_height - pads.top;

    // 在图像周围添加填充
    cv::copyMakeBorder(resized_image, padded_image, pads.top, pads.bottom, pads.left, pads.right, cv::BORDER_CONSTANT, pad_color);


}