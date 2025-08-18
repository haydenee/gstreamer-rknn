#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "rknnprocess.h"
#include "im2d_type.h"
#include "rknn_api.h"
#include "cnpy/cnpy.h"
#include "postprocess.h"
#include "rgaprocess.h"

GST_DEBUG_CATEGORY_EXTERN(gst_plugin_rknn_debug);
#define GST_CAT_DEFAULT gst_plugin_rknn_debug

static unsigned char* load_data(FILE* fp, size_t ofst, size_t sz);
static unsigned char* load_model(const char* filename, int* model_size);
static void dump_tensor_attr(rknn_tensor_attr* attr);

static float boxes_exp[256];
int rknn_prepare(struct RknnEngine* rknn_process, rknn_context* shared_context, int core)
{
    int ret;
    /* Create the neural network */
    GST_DEBUG("Loading model...");
    int model_data_size = 0;
    
    // 初始化上下文为0（空值）
    rknn_process->ctx = 0;
    
    // 如果提供了共享上下文且不为0，则复用它
    bool is_first_init;
    if (shared_context != NULL && *shared_context != 0) {
        GST_DEBUG("Duplicating shared RKNN context");
        ret = rknn_dup_context(shared_context, &rknn_process->ctx);
        if (ret < 0) {
            GST_ERROR("rknn_dup_context error ret=%d", ret);
            return -1;
        }
        is_first_init = false;
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
        is_first_init = true;
    }
    rknn_set_core_mask(rknn_process->ctx, (rknn_core_mask)(1 << core));
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
        channel = attr.dims[1];
        height = attr.dims[2];
        width = attr.dims[3];
    } else {
        GST_DEBUG("model is NHWC input fmt");
        height = attr.dims[1];
        width = attr.dims[2];
        channel = attr.dims[3];
    }
    
    GST_DEBUG("model input height=%d, width=%d, channel=%d", height, width, channel);

    rknn_process->inputs = (rknn_input*)malloc(sizeof(rknn_input) * rknn_process->io_num.n_input);
    memset(rknn_process->inputs, 0, sizeof(rknn_input) * rknn_process->io_num.n_input);
    rknn_process->inputs[0].index = attr.index;
    rknn_process->inputs[0].type = RKNN_TENSOR_UINT8;
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

    int zp = rknn_process->output_attrs[1].zp;
    float scale = rknn_process->output_attrs[1].scale;
    if (is_first_init) {
        fill_exp_table(boxes_exp, zp, scale);

    }
    return 0;
}

int rknn_inference(struct RknnEngine* rknn_process)
{
    int ret = 0;
    // 执行推理

    rknn_inputs_set(rknn_process->ctx, rknn_process->io_num.n_input, rknn_process->inputs);
    ret = rknn_run(rknn_process->ctx, NULL);
    ret = rknn_outputs_get(rknn_process->ctx, rknn_process->io_num.n_output, rknn_process->outputs, NULL);

    return ret;
}

int rknn_postprocess(
    struct RknnEngine* rknn_process,
    float box_conf_threshold,
    float nms_threshold
)
{
    ResultData data;
    data.boxes_confs.data = (int8_t*)rknn_process->outputs[0].buf;
    data.boxes_confs.zp = rknn_process->output_attrs[0].zp;
    data.boxes_confs.scale = rknn_process->output_attrs[0].scale;

    data.boxes.data = (int8_t*)rknn_process->outputs[1].buf;
    data.boxes.zp = rknn_process->output_attrs[1].zp;
    data.boxes.scale = rknn_process->output_attrs[1].scale;

    data.kpts_confs.data = (int8_t*)rknn_process->outputs[2].buf;    
    data.kpts_confs.zp = rknn_process->output_attrs[2].zp;
    data.kpts_confs.scale = rknn_process->output_attrs[2].scale;

    data.kpts.data = (int8_t*)rknn_process->outputs[3].buf;
    data.kpts.zp = rknn_process->output_attrs[3].zp;
    data.kpts.scale = rknn_process->output_attrs[3].scale;

    data.height = rknn_process->model_height;
    data.width = rknn_process->model_width;
    data.count = data.height * data.width * 21 / 1024;
    data.boxes_exp = boxes_exp;
    post_process(box_conf_threshold, nms_threshold, &data);
    
    auto& r = rknn_process->results;
    rknn_process->results_size = data.results.size();
    float scale = rknn_process->scale_model_to_img;
    int model_pad_left = rknn_process->model_pad_left;
    int model_pad_top = rknn_process->model_pad_top;
    int img_pad_left = rknn_process->img_pad_left;
    int img_pad_top = rknn_process->img_pad_top;
    for (int i = 0; i < rknn_process->results_size; i++) {
        auto& person = data.results[i];
        r[i][0] = (person.box.rect.x - model_pad_left) * scale - img_pad_left;
        r[i][1] = (person.box.rect.y - model_pad_top) * scale - img_pad_top;
        r[i][2] = person.box.rect.w * scale;
        r[i][3] = person.box.rect.h * scale;
        r[i][4] = person.box.conf;
        for (int j = 0; j < 17; j++) {
            r[i][5 + j * 3 + 0] = (person.kpts[j * 3 + 0] - model_pad_left) * scale + img_pad_left;
            r[i][5 + j * 3 + 1] = (person.kpts[j * 3 + 1] - model_pad_top) * scale + img_pad_top;
            r[i][5 + j * 3 + 2] = person.kpts[j * 3 + 2];
        }
    }
    
    return 0;
}

#define COLOR_GREEN     0xFF00FF00
#define COLOR_BLUE      0xFFFF0000
#define COLOR_RED       0xFF0000FF
#define COLOR_YELLOW    0xFF00FFFF
#define COLOR_ORANGE    0xFF0045FF
#define COLOR_BLACK     0xFF000000
#define COLOR_WHITE     0xFFFFFFFF
void rknn_visualize(
    struct RknnEngine* rknn_process,
    GstBuffer* output)
{
    const int boxes_num = rknn_process->results_size;  
    const int kpts_num = 17 * boxes_num;
    rga_buffer_t rga_buf;
    gst_buffer_to_rga_buffer(output, &rga_buf);

    im_rect* boxes_rect_arr = (im_rect*)malloc(sizeof(im_rect) * boxes_num);
    im_rect* kpts_rect_arr = (im_rect*)malloc(sizeof(im_rect) * kpts_num);
    GST_DEBUG("rknn_visualize");
    // 画框
    for (int i = 0; i < rknn_process->results_size; i++) {
        float* data_entry = rknn_process->results[i];
        float x1 = data_entry[0];
        float y1 = data_entry[1];
        float w = data_entry[2];
        float h = data_entry[3];
        float x2 = x1 + w;
        float y2 = y1 + h;
        // 需要横纵坐标都被2整除
        int x1i = 2 * int(x1 / 2);
        int x2i = 2 * int(x2 / 2);
        int y1i = 2 * int(y1 / 2);
        int y2i = 2 * int(y2 / 2);
        boxes_rect_arr[i] = {x1i, y1i, x2i - x1i, y2i - y1i};
        GST_TRACE("box conf %.2f @ (%.2f %.2f %.2f %.2f)", data_entry[4], x1, y1, x2, y2);
        // 17 个关键点
        for (int j = 0; j < 17; j++) {
            float x = data_entry[j * 3 + 5];
            float y = data_entry[j * 3 + 6];
            int xi = 2 * int(x / 2);
            int yi = 2 * int(y / 2);
            kpts_rect_arr[i * 17 + j] = {xi - 4, yi - 4, 8, 8};
            GST_TRACE("> point conf %.2f @ (%.2f, %.2f)", data_entry[j * 3 + 7], x, y);
        }
    }

    GST_DEBUG("worker %d rknn_visualize start", rknn_process->worker_id);
    // GstMapInfo map_info;
    // gst_buffer_map(output, &map_info, GST_MAP_READWRITE);
    
    while (imrectangleArray( rga_buf, boxes_rect_arr, boxes_num, COLOR_GREEN, 4) != IM_STATUS_SUCCESS) {
        GST_WARNING("imrectangleArray failed, retry in 1000us");
        usleep(1000);
    }
    while (imfillArray( rga_buf, kpts_rect_arr, kpts_num, COLOR_RED) != IM_STATUS_SUCCESS) {
        GST_WARNING("imfillArray failed, retry in 1000us");
        usleep(1000);
    };
    // gst_buffer_unmap(output, &map_info);
    free(boxes_rect_arr);
    free(kpts_rect_arr);
    GST_DEBUG("worker %d rknn_visualize done", rknn_process->worker_id);
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

static void dump_tensor_attr(rknn_tensor_attr* attr)
{
    std::string shape_str = attr->n_dims < 1 ? "" : std::to_string(attr->dims[0]);
    for (uint i = 1; i < attr->n_dims; ++i) {
        shape_str += ", " + std::to_string(attr->dims[i]);
    }

    GST_TRACE("  index=%d, name=%s, n_dims=%d, dims=[%s], n_elems=%d, size=%d, w_stride = %d, size_with_stride=%d, fmt=%s, "
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

static void dump_tensor_data(const char* prefix, uint index, const rknn_tensor_attr* attr, const void* data, size_t size)
{
    // 构造形状字符串
    std::string shape_str = "";
    for (uint j = 0; j < attr->n_dims; j++) {
        if (j > 0) shape_str += "x";
        shape_str += std::to_string(attr->dims[j]);
    }
    
    // 根据数据类型确定 numpy 数据类型
    std::string dtype_str;
    
    switch (attr->type) {
        case RKNN_TENSOR_INT8:
            dtype_str = "int8";
            break;
        case RKNN_TENSOR_UINT8:
            dtype_str = "uint8";
            break;
        case RKNN_TENSOR_INT16:
            dtype_str = "int16";
            break;
        case RKNN_TENSOR_UINT16:
            dtype_str = "uint16";
            break;
        case RKNN_TENSOR_INT32:
            dtype_str = "int32";
            break;
        case RKNN_TENSOR_UINT32:
            dtype_str = "uint32";
            break;
        case RKNN_TENSOR_FLOAT32:
            dtype_str = "float32";
            break;
        case RKNN_TENSOR_FLOAT16:
            dtype_str = "float16";
            break;
        default:
            GST_WARNING("Unsupported tensor type: %d", attr->type);
            return;
    }
    
    // 构造文件名，包含 dtype 和 shape 信息
    char filename[256];
    snprintf(filename, sizeof(filename), "./dumps/%s_%d_%s_%s.npy", prefix, index, dtype_str.c_str(), shape_str.c_str());
    
    // 构造形状向量
    std::vector<size_t> shape;
    for (uint j = 0; j < attr->n_dims; j++) {
        shape.push_back(attr->dims[j]);
    }
    
    // 保存数据到 npy 文件
    if (attr->type == RKNN_TENSOR_FLOAT16) {
        // 特殊处理 float16，转换为 float32 保存
        size_t num_elements = size / sizeof(uint16_t);
        std::vector<float> converted_data(num_elements);
        
        // 简单的 float16 到 float32 转换（这里只是示例，实际转换可能更复杂）
        uint16_t* src = (uint16_t*)data;
        for (size_t j = 0; j < num_elements; j++) {
            // 这是一个简化的转换，实际的 float16 到 float32 转换需要考虑指数和尾数
            converted_data[j] = (float)src[j];
        }
        
        cnpy::npy_save(filename, converted_data.data(), shape, "w");
    } else {
        // 直接保存其他类型的数据
        cnpy::npy_save(filename, (char*)data, shape, "w");
    }
    
    // 创建同名的 txt 文件，保存 zp 和 scale 信息
    char txt_filename[256];
    snprintf(txt_filename, sizeof(txt_filename), "./dumps/%s_%d_%s_%s.txt", prefix, index, dtype_str.c_str(), shape_str.c_str());
    
    FILE* txt_file = fopen(txt_filename, "w");
    if (txt_file != NULL) {
        fprintf(txt_file, "%d %.8f", attr->zp, attr->scale);
        fclose(txt_file);
        GST_TRACE("Saved %s tensor %d zp and scale to %s", prefix, index, txt_filename);
    } else {
        GST_ERROR("Failed to create txt file %s", txt_filename);
    }
    
    GST_TRACE("Saved %s tensor %d to %s", prefix, index, filename);
}

void rknn_dump_io(struct RknnEngine* rknn_process)
{
    // 创建 dumps 目录（如果不存在）
    system("mkdir -p ./dumps");
    
    // Dump 输入张量
    for (uint i = 0; i < rknn_process->io_num.n_input; i++) {
        const rknn_tensor_attr attr = rknn_process->input_attrs[i];
        const rknn_input input = rknn_process->inputs[i];
        dump_tensor_data("input", i, &attr, input.buf, input.size);
    }
    
    // Dump 输出张量
    for (uint i = 0; i < rknn_process->io_num.n_output; i++) {
        const rknn_tensor_attr attr = rknn_process->output_attrs[i];
        const rknn_output output = rknn_process->outputs[i];
        dump_tensor_data("output", i, &attr, output.buf, output.size);
    }
}
