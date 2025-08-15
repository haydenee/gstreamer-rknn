#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <gst/gst.h>

#include "rknnprocess.h"
#include "postprocess.h"
#include "common_structs.h"
#include "rknn_api.h"
#include "rgaprocess.h"


GST_DEBUG_CATEGORY_EXTERN(gst_plugin_rknn_debug);
#define GST_CAT_DEFAULT gst_plugin_rknn_debug

static unsigned char* load_data(FILE* fp, size_t ofst, size_t sz);
static unsigned char* load_model(const char* filename, int* model_size);
static void dump_tensor_attr(rknn_tensor_attr* attr);
#ifdef __cplusplus
extern "C" {
#endif


int rknn_prepare(struct RknnEngine* rknn_process, rknn_context* shared_context)
{
    int ret;
    /* Create the neural network */
    GST_DEBUG("Loading model...");
    int model_data_size = 0;
    
    // 初始化上下文为0（空值）
    rknn_process->ctx = 0;
    
    // 如果提供了共享上下文且不为0，则复用它
    if (shared_context != NULL && *shared_context != 0) {
        GST_DEBUG("Duplicating shared RKNN context");
        ret = rknn_dup_context(shared_context, &rknn_process->ctx);
        if (ret < 0) {
            GST_ERROR("rknn_dup_context error ret=%d", ret);
            return -1;
        }
        // 复用上下文时不需要重新加载模型数据
        rknn_process->model_data = NULL;
    } else {
        // 否则初始化新的上下文
        if (!rknn_process->model_path) {
            GST_ERROR("model_path is NULL");
            return -1;
        }
        GST_DEBUG("Initializing new RKNN context");
        rknn_process->model_data = load_model(rknn_process->model_path, &model_data_size);
        ret = rknn_init(&rknn_process->ctx, rknn_process->model_data, model_data_size, 0, NULL);
        if (ret < 0) {
            GST_ERROR("rknn_init error ret=%d", ret);
            return -1;
        }
    }

    rknn_sdk_version version;
    ret = rknn_query(rknn_process->ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(rknn_sdk_version));
    if (ret < 0) {
        GST_ERROR("rknn_init error ret=%d", ret);
        return -1;
    }
    GST_DEBUG("sdk version: %s driver version: %s", version.api_version, version.drv_version);

    ret = rknn_query(rknn_process->ctx, RKNN_QUERY_IN_OUT_NUM, &rknn_process->io_num, sizeof(rknn_process->io_num));
    if (ret < 0) {
        GST_ERROR("rknn_init error ret=%d", ret);
        return -1;
    }
    GST_DEBUG("model input num: %d, output num: %d", rknn_process->io_num.n_input, rknn_process->io_num.n_output);

    rknn_process->input_attrs = (rknn_tensor_attr*)malloc(sizeof(rknn_tensor_attr) * rknn_process->io_num.n_input);
    memset(rknn_process->input_attrs, 0, sizeof(rknn_tensor_attr) * rknn_process->io_num.n_input);
    for (uint i = 0; i < rknn_process->io_num.n_input; i++) {
        rknn_process->input_attrs[i].index = i;
        ret = rknn_query(rknn_process->ctx, RKNN_QUERY_INPUT_ATTR, &(rknn_process->input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0) {
            GST_ERROR("rknn_init error ret=%d", ret);
            return -1;
        }
        dump_tensor_attr(&(rknn_process->input_attrs[i]));
    }

    rknn_process->output_attrs = (rknn_tensor_attr*)malloc(sizeof(rknn_tensor_attr) * rknn_process->io_num.n_output);
    memset(rknn_process->output_attrs, 0, sizeof(rknn_tensor_attr) * rknn_process->io_num.n_output);
    for (uint i = 0; i < rknn_process->io_num.n_output; i++) {
        rknn_process->output_attrs[i].index = i;
        ret = rknn_query(rknn_process->ctx, RKNN_QUERY_OUTPUT_ATTR, &(rknn_process->output_attrs[i]), sizeof(rknn_tensor_attr));
        dump_tensor_attr(&(rknn_process->output_attrs[i]));
    }

    int channel = 3;
    int width = 0;
    int height = 0;
    const rknn_tensor_attr attr = rknn_process->input_attrs[0];
    if (attr.fmt == RKNN_TENSOR_NCHW) {
        GST_DEBUG("model is NCHW input fmt");
    } else {
        GST_DEBUG("model is NHWC input fmt");
    }
    
    channel = attr.dims[1];
    height = attr.dims[2];
    width = attr.dims[3];

    GST_DEBUG("model input height=%d, width=%d, channel=%d", height, width, channel);

    rknn_process->inputs = (rknn_input*)malloc(sizeof(rknn_input) * rknn_process->io_num.n_input);
    memset(rknn_process->inputs, 0, sizeof(rknn_input) * rknn_process->io_num.n_input);
    rknn_process->inputs[0].index = attr.index;
    rknn_process->inputs[0].type = attr.type;
    rknn_process->inputs[0].size = attr.size;
    rknn_process->inputs[0].pass_through = 0;
    rknn_process->inputs[0].fmt = attr.fmt;

    rknn_process->model_width = width;
    rknn_process->model_height = height;
    rknn_process->model_channel = channel;

    // TODO: Prealloc output memory
    rknn_process->outputs = (rknn_output*)malloc(sizeof(rknn_output) * rknn_process->io_num.n_output);
    memset(rknn_process->outputs, 0, sizeof(rknn_output) * rknn_process->io_num.n_output);
    for (uint i = 0; i < rknn_process->io_num.n_output; i++) {
        const rknn_tensor_attr attr = rknn_process->output_attrs[i];
        rknn_process->outputs[i].index = attr.index;
        rknn_process->outputs[i].want_float = 0; // 默认不需要
        rknn_process->outputs[i].is_prealloc = false;
        rknn_process->outputs[i].size = attr.size;
    }

    return 0;
}

int rknn_inference(
    struct RknnEngine* rknn_process,
    int do_inference
)
{
    int ret = 0;

    if (do_inference) {
        rknn_inputs_set(rknn_process->ctx, rknn_process->io_num.n_input, rknn_process->inputs);
        // 执行推理
        ret = rknn_run(rknn_process->ctx, NULL);
        ret = rknn_outputs_get(rknn_process->ctx, rknn_process->io_num.n_output, rknn_process->outputs, NULL);
    }

    return ret;
}

int rknn_postprocess(
    struct RknnEngine* rknn_process,
    float box_conf_threshold,
    float nms_threshold,
    detect_result_group_t* detect_result_group
)
{
    std::vector<float> out_scales;
    std::vector<int32_t> out_zps;
    for (uint i = 0; i < rknn_process->io_num.n_output; ++i) {
        out_scales.push_back(rknn_process->output_attrs[i].scale);
        out_zps.push_back(rknn_process->output_attrs[i].zp);
    }
    return post_process(
        (int8_t*)rknn_process->outputs[0].buf,
        (int8_t*)rknn_process->outputs[1].buf,
        (int8_t*)rknn_process->outputs[2].buf,
        rknn_process->model_height,
        rknn_process->model_width,
        box_conf_threshold,
        nms_threshold,
        rknn_process->pads,
        rknn_process->scale,
        rknn_process->scale,
        out_zps,
        out_scales,
        detect_result_group,
        NULL
    );
    rknn_outputs_release(rknn_process->ctx, rknn_process->io_num.n_output, rknn_process->outputs);
}

void rknn_visualize(
    struct RknnEngine* rknn_process,
    GstBuffer* output,
    detect_result_group_t* detect_result_group)
{
    // GST_DEBUG("opencv %d %d %d", rknn_process->original_width, rknn_process->original_height, GST_ROUND_UP_16(rknn_process->original_width) * 3);
    rga_buffer_t rga_buf;
    gst_buffer_to_rga_buffer(output, &rga_buf);

    // 画框和概率
    char text[256];
    for (int i = 0; i < detect_result_group->count; i++) {
        detect_result_t* det_result = &(detect_result_group->results[i]);
        sprintf(text, "%s %.1f%%", det_result->name, det_result->prop * 100);
        GST_TRACE("%s @ (%d %d %d %d) %f", det_result->name, det_result->box.left, det_result->box.top,
             det_result->box.right, det_result->box.bottom, det_result->prop);
        int x1 = det_result->box.left;
        int y1 = det_result->box.top;
        int x2 = det_result->box.right;
        int y2 = det_result->box.bottom;
        im_rect rect;
        rect.x = GST_ROUND_UP_2(x1);
        rect.y = GST_ROUND_UP_2(y1);
        rect.width = GST_ROUND_UP_2(x2 - x1);
        rect.height = GST_ROUND_UP_2(y2 - y1);
        imrectangle(rga_buf, rect, 0x00ff0000, 6);
    }
}

void rknn_release(struct RknnEngine* rknn_process) 
{
    if (rknn_process->input_attrs) free(rknn_process->input_attrs);
    if (rknn_process->output_attrs) free(rknn_process->output_attrs);
    if (rknn_process->inputs) free(rknn_process->inputs);
    if (rknn_process->outputs) free(rknn_process->outputs);
    rknn_process->input_attrs = nullptr;
    rknn_process->output_attrs = nullptr;
    rknn_process->inputs = nullptr;
    rknn_process->outputs = nullptr;
    rknn_destroy(rknn_process->ctx);
    if(rknn_process->model_data) {
        free(rknn_process->model_data);
    }
}
#ifdef __cplusplus
}
#endif

static void dump_tensor_attr(rknn_tensor_attr* attr)
{
    std::string shape_str = attr->n_dims < 1 ? "" : std::to_string(attr->dims[0]);
    for (uint i = 1; i < attr->n_dims; ++i) {
        shape_str += ", " + std::to_string(attr->dims[i]);
    }

    GST_DEBUG("  index=%d, name=%s, n_dims=%d, dims=[%s], n_elems=%d, size=%d, w_stride = %d, size_with_stride=%d, fmt=%s, "
           "type=%s, qnt_type=%s, "
           "zp=%d, scale=%f",
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
        printf("blob seek failure.");
        return NULL;
    }

    data = (unsigned char*)malloc(sz);
    if (data == NULL) {
        printf("buffer malloc failure.");
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
        printf("Open file %s failed.", filename);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);

    data = load_data(fp, 0, size);

    fclose(fp);

    *model_size = size;
    return data;
}
