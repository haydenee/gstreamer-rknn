#ifndef _RKNN_PROCESS_H_
#define _RKNN_PROCESS_H_
#include "rknn_api.h"
#include "common_structs.h"

struct RknnEngine {
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

int rknn_prepare(struct RknnEngine* rknn_process);
int rknn_inference(
    struct RknnEngine* rknn_process,
    int do_inference   // 新增参数：是否执行推理，0=不推理，1=推理
);

int rknn_postprocess(
    struct RknnEngine* rknn_process,
    float box_conf_threshold,
    float nms_threshold,
    detect_result_group_t* detect_result_group
);

void rknn_visualize(
    struct RknnEngine* rknn_process,
    void* orig_img,
    detect_result_group_t* detect_result_group,
    int show_fps,
    double current_fps
);
void rknn_release(struct RknnEngine* rknn_process);
#ifdef __cplusplus
}
#endif

#endif