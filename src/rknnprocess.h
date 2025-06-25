#ifndef _RKNN_PROCESS_H_
#define _RKNN_PROCESS_H_
#include "rknn_api.h"

typedef struct _BOX_RECT
{
    int left;
    int right;
    int top;
    int bottom;
} BOX_RECT;
struct _RknnProcess {
    rknn_context ctx;
    rknn_input* inputs;
    rknn_output* outputs;
    rknn_input_output_num io_num;
    rknn_tensor_attr *input_attrs;
    rknn_tensor_attr *output_attrs;
    int model_width;
    int model_height;
    int model_channel;
    BOX_RECT pads;
    float scale_w;
    float scale_h;
    int original_width;
    int original_height;
    unsigned char* model_data;
    char* label_path;
    char* model_path;
};

#ifdef __cplusplus
extern "C" {
#endif

int rknn_prepare(struct _RknnProcess* rknn_process);
int rknn_inference_and_postprocess(
    struct _RknnProcess* rknn_process,
    void* orig_img,
    float box_conf_threshold,
    float nms_threshold,
    int show_fps,      
    double current_fps      
);
void rknn_release(struct _RknnProcess* rknn_process);
#ifdef __cplusplus
}
#endif

#endif