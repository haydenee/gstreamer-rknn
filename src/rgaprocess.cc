#include "rgaprocess.h"
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"

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