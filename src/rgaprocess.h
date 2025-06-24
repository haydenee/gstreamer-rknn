#ifndef _RGA_PROCESS_H_
#define _RGA_PROCESS_H_
#include "RgaUtils.h"
#include "gst/video/video-format.h"
#include "im2d.h"
#include "rga.h"

typedef struct _BOX_RECT {
    int left;
    int right;
    int top;
    int bottom;
} BOX_RECT;

#ifdef __cplusplus
extern "C" {
#endif

RgaSURF_FORMAT gst_to_rga_format(GstVideoFormat gst_format);
GstVideoFormat rga_to_gst_format(RgaSURF_FORMAT rga_format);

#ifdef __cplusplus
}
#endif


#endif