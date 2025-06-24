#include "rknnprocess.h"
#include "opencv2/core/mat.hpp"
#include "opencv2/opencv.hpp"
#include "postprocess.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

static unsigned char* load_data(FILE* fp, size_t ofst, size_t sz);
static unsigned char* load_model(const char* filename, int* model_size);
static void dump_tensor_attr(rknn_tensor_attr* attr);
#ifdef __cplusplus
extern "C" {
#endif

int rknn_prepare(const char* model_name, struct _RknnProcess* rknn_process)
{
    int ret;
    /* Create the neural network */
    printf("Loading mode...\n");
    int model_data_size = 0;
    unsigned char* model_data = load_model(model_name, &model_data_size);
    ret = rknn_init(&rknn_process->ctx, model_data, model_data_size, 0, NULL);
    if (ret < 0) {
        printf("rknn_init error ret=%d\n", ret);
        return -1;
    }

    rknn_sdk_version version;
    ret = rknn_query(rknn_process->ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(rknn_sdk_version));
    if (ret < 0) {
        printf("rknn_init error ret=%d\n", ret);
        return -1;
    }
    printf("sdk version: %s driver version: %s\n", version.api_version, version.drv_version);

    ret = rknn_query(rknn_process->ctx, RKNN_QUERY_IN_OUT_NUM, &rknn_process->io_num, sizeof(rknn_process->io_num));
    if (ret < 0) {
        printf("rknn_init error ret=%d\n", ret);
        return -1;
    }
    printf("model input num: %d, output num: %d\n", rknn_process->io_num.n_input, rknn_process->io_num.n_output);

    rknn_process->input_attrs = (rknn_tensor_attr*)malloc(sizeof(rknn_tensor_attr) * rknn_process->io_num.n_input);
    memset(rknn_process->input_attrs, 0, sizeof(rknn_tensor_attr) * rknn_process->io_num.n_input);
    for (int i = 0; i < rknn_process->io_num.n_input; i++) {
        rknn_process->input_attrs[i].index = i;
        ret = rknn_query(rknn_process->ctx, RKNN_QUERY_INPUT_ATTR, &(rknn_process->input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0) {
            printf("rknn_init error ret=%d\n", ret);
            return -1;
        }
        dump_tensor_attr(&(rknn_process->input_attrs[i]));
    }

    rknn_process->output_attrs = (rknn_tensor_attr*)malloc(sizeof(rknn_tensor_attr) * rknn_process->io_num.n_output);
    memset(rknn_process->output_attrs, 0, sizeof(rknn_tensor_attr) * rknn_process->io_num.n_output);
    for (int i = 0; i < rknn_process->io_num.n_output; i++) {
        rknn_process->output_attrs[i].index = i;
        ret = rknn_query(rknn_process->ctx, RKNN_QUERY_OUTPUT_ATTR, &(rknn_process->output_attrs[i]), sizeof(rknn_tensor_attr));
        dump_tensor_attr(&(rknn_process->output_attrs[i]));
    }

    int channel = 3;
    int width = 0;
    int height = 0;
    if (rknn_process->input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
        printf("model is NCHW input fmt\n");
        channel = rknn_process->input_attrs[0].dims[1];
        height = rknn_process->input_attrs[0].dims[2];
        width = rknn_process->input_attrs[0].dims[3];
    } else {
        printf("model is NHWC input fmt\n");
        height = rknn_process->input_attrs[0].dims[1];
        width = rknn_process->input_attrs[0].dims[2];
        channel = rknn_process->input_attrs[0].dims[3];
    }

    printf("model input height=%d, width=%d, channel=%d\n", height, width, channel);

    rknn_process->inputs = (rknn_input*)malloc(sizeof(rknn_input) * rknn_process->io_num.n_input);
    memset(rknn_process->inputs, 0, sizeof(rknn_input) * rknn_process->io_num.n_input);
    rknn_process->inputs[0].index = 0;
    rknn_process->inputs[0].type = RKNN_TENSOR_UINT8;
    rknn_process->inputs[0].size = width * height * channel;
    rknn_process->inputs[0].fmt = RKNN_TENSOR_NHWC;
    rknn_process->inputs[0].pass_through = 0;

    rknn_process->model_width = width;
    rknn_process->model_height = height;
    rknn_process->model_channel = channel;

    rknn_process->outputs = (rknn_output*)malloc(sizeof(rknn_output) * rknn_process->io_num.n_output);
    memset(rknn_process->outputs, 0, sizeof(rknn_output) * rknn_process->io_num.n_output);
    for (int i = 0; i < rknn_process->io_num.n_output; i++) {
        rknn_process->outputs[i].index = i;
        rknn_process->outputs[i].want_float = 0; // 默认不需要
    }

    return 0;
}

void rknn_inference_and_postprocess(
    struct _RknnProcess* rknn_process,
    void* orig_img,
    float box_conf_threshold,
    float nms_threshold)
{
    int ret;

    cv::Mat orig_img_cv(rknn_process->original_height, rknn_process->original_width, CV_8UC3, orig_img);

    rknn_inputs_set(rknn_process->ctx, rknn_process->io_num.n_input, rknn_process->inputs);
    // 执行推理
    ret = rknn_run(rknn_process->ctx, NULL);
    ret = rknn_outputs_get(rknn_process->ctx, rknn_process->io_num.n_output, rknn_process->outputs, NULL);

    // 后处理
    detect_result_group_t detect_result_group;
    std::vector<float> out_scales;
    std::vector<int32_t> out_zps;
    for (int i = 0; i < rknn_process->io_num.n_output; ++i) {
        out_scales.push_back(rknn_process->output_attrs[i].scale);
        out_zps.push_back(rknn_process->output_attrs[i].zp);
    }
    post_process(
        (int8_t*)rknn_process->outputs[0].buf,
        (int8_t*)rknn_process->outputs[1].buf,
        (int8_t*)rknn_process->outputs[2].buf,
        rknn_process->model_height,
        rknn_process->model_width,
        box_conf_threshold,
        nms_threshold,
        rknn_process->pads,
        rknn_process->scale_w,
        rknn_process->scale_h,
        out_zps,
        out_scales,
        &detect_result_group);

    // 画框和概率
    char text[256];
    for (int i = 0; i < detect_result_group.count; i++) {
        detect_result_t* det_result = &(detect_result_group.results[i]);
        sprintf(text, "%s %.1f%%", det_result->name, det_result->prop * 100);
        printf("%s @ (%d %d %d %d) %f\n", det_result->name, det_result->box.left, det_result->box.top,
            det_result->box.right, det_result->box.bottom, det_result->prop);
        int x1 = det_result->box.left;
        int y1 = det_result->box.top;
        int x2 = det_result->box.right;
        int y2 = det_result->box.bottom;
        rectangle(orig_img_cv, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(256, 0, 0, 256), 3);
        putText(orig_img_cv, text, cv::Point(x1, y1 + 12), cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(255, 255, 255));
    }

    // imwrite("inference.bmp", orig_img_cv);

    // 释放推理结果
    rknn_outputs_release(rknn_process->ctx, rknn_process->io_num.n_output, rknn_process->outputs);
}

#ifdef __cplusplus
}
#endif
static void dump_tensor_attr(rknn_tensor_attr* attr)
{
    std::string shape_str = attr->n_dims < 1 ? "" : std::to_string(attr->dims[0]);
    for (int i = 1; i < attr->n_dims; ++i) {
        shape_str += ", " + std::to_string(attr->dims[i]);
    }

    printf("  index=%d, name=%s, n_dims=%d, dims=[%s], n_elems=%d, size=%d, w_stride = %d, size_with_stride=%d, fmt=%s, "
           "type=%s, qnt_type=%s, "
           "zp=%d, scale=%f\n",
        attr->index, attr->name, attr->n_dims, shape_str.c_str(), attr->n_elems, attr->size, attr->w_stride,
        attr->size_with_stride, get_format_string(attr->fmt), get_type_string(attr->type),
        get_qnt_type_string(attr->qnt_type), attr->zp, attr->scale);
}

static unsigned char* load_data(FILE* fp, size_t ofst, size_t sz)
{
    unsigned char* data;
    int ret;

    data = NULL;

    if (NULL == fp) {
        return NULL;
    }

    ret = fseek(fp, ofst, SEEK_SET);
    if (ret != 0) {
        printf("blob seek failure.\n");
        return NULL;
    }

    data = (unsigned char*)malloc(sz);
    if (data == NULL) {
        printf("buffer malloc failure.\n");
        return NULL;
    }
    ret = fread(data, 1, sz, fp);
    return data;
}

static unsigned char* load_model(const char* filename, int* model_size)
{
    FILE* fp;
    unsigned char* data;

    fp = fopen(filename, "rb");
    if (NULL == fp) {
        printf("Open file %s failed.\n", filename);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);

    data = load_data(fp, 0, size);

    fclose(fp);

    *model_size = size;
    return data;
}
