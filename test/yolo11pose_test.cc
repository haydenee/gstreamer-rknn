#include <arm_neon.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <gst/allocators/gstfdmemory.h>
#include <opencv2/opencv.hpp>
#include "opencv2/core.hpp"
#include "rknn_api.h"
#include "rknnprocess.h"

static unsigned char* load_model(const char* filename, int* model_size)
{
    int fd;
    struct stat sb;
    unsigned char* data;

    // 打开文件
    fd = open(filename, O_RDONLY);
    if (fd == -1) {
        printf("Open file %s failed.\n", filename);
        return NULL;
    }

    // 获取文件大小
    if (fstat(fd, &sb) == -1) {
        printf("Get file size failed.\n");
        close(fd);
        return NULL;
    }

    // 映射文件到内存
    data = (unsigned char*)mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        printf("mmap failed.\n");
        close(fd);
        return NULL;
    }

    // 关闭文件描述符，映射的内存仍然有效
    close(fd);

    *model_size = sb.st_size;
    return data;
}

static void dump_tensor_attr(rknn_tensor_attr* attr)
{
    char shape_str[256] = {0};
    
    if (attr->n_dims < 1) {
        shape_str[0] = '\0';
    } else {
        sprintf(shape_str, "%d", attr->dims[0]);
        for (uint32_t i = 1; i < attr->n_dims; ++i) {
            char temp[32];
            sprintf(temp, ", %d", attr->dims[i]);
            strcat(shape_str, temp);
        }
    }

    printf("  index=%d, name=%s, n_dims=%d, dims=[%s], n_elems=%d, size=%d, w_stride = %d, size_with_stride=%d, fmt=%s, "
           "type=%s, qnt_type=%s, "
           "zp=%d, scale=%f\n",
        attr->index, attr->name, attr->n_dims, shape_str, attr->n_elems, attr->size, attr->w_stride,
        attr->size_with_stride, get_format_string(attr->fmt), get_type_string(attr->type),
        get_qnt_type_string(attr->qnt_type), attr->zp, attr->scale);
}

int main() {

    cv::Mat img = cv::imread("/root/rknn_input.bmp");
    cv::Mat img_fp16;
    cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
    img.convertTo(img_fp16, CV_16FC3);
    RknnEngine* rknn_process = (RknnEngine*)malloc(sizeof(struct RknnEngine));
    const char* model_path = "model/yolo11s-pose_rknn_model/yolo11s-pose-rk3588.rknn";
    int model_data_size = 0;
    rknn_process->model_data = load_model(model_path, &model_data_size);
    rknn_init(&rknn_process->ctx, rknn_process->model_data, model_data_size, 0, NULL);
    rknn_sdk_version version;
    rknn_query(rknn_process->ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(rknn_sdk_version));
    printf("sdk version: %s driver version: %s\n", version.api_version, version.drv_version);
    rknn_query(rknn_process->ctx, RKNN_QUERY_IN_OUT_NUM, &rknn_process->io_num, sizeof(rknn_process->io_num));
    printf("model input num: %d, output num: %d\n", rknn_process->io_num.n_input, rknn_process->io_num.n_output);
    rknn_process->input_attrs = (rknn_tensor_attr*)malloc(sizeof(rknn_tensor_attr) * rknn_process->io_num.n_input);
    memset(rknn_process->input_attrs, 0, sizeof(rknn_tensor_attr) * rknn_process->io_num.n_input);
    for (uint i = 0; i < rknn_process->io_num.n_input; i++) {
        rknn_process->input_attrs[i].index = i;
        rknn_query(rknn_process->ctx, RKNN_QUERY_INPUT_ATTR, &(rknn_process->input_attrs[i]), sizeof(rknn_tensor_attr));
        dump_tensor_attr(&(rknn_process->input_attrs[i]));
    }
    rknn_process->output_attrs = (rknn_tensor_attr*)malloc(sizeof(rknn_tensor_attr) * rknn_process->io_num.n_output);
    memset(rknn_process->output_attrs, 0, sizeof(rknn_tensor_attr) * rknn_process->io_num.n_output);
    for (uint i = 0; i < rknn_process->io_num.n_output; i++) {
        rknn_process->output_attrs[i].index = i;
        rknn_query(rknn_process->ctx, RKNN_QUERY_OUTPUT_ATTR, &(rknn_process->output_attrs[i]), sizeof(rknn_tensor_attr));
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
    int ret = 0;

    rknn_process->inputs = (rknn_input*)malloc(sizeof(rknn_input) * rknn_process->io_num.n_input);
    memset(rknn_process->inputs, 0, sizeof(rknn_input) * rknn_process->io_num.n_input);
    rknn_process->inputs[0].index = 0;
    rknn_process->inputs[0].type = rknn_process->input_attrs->type;
    rknn_process->inputs[0].size = rknn_process->input_attrs->size;
    rknn_process->inputs[0].fmt = rknn_process->input_attrs->fmt;
    rknn_process->inputs[0].pass_through = 0;
    rknn_process->inputs[0].buf = img_fp16.data;

    rknn_process->model_width = width;
    rknn_process->model_height = height;
    rknn_process->model_channel = channel;

    rknn_process->outputs = (rknn_output*)malloc(sizeof(rknn_output) * rknn_process->io_num.n_output);
    memset(rknn_process->outputs, 0, sizeof(rknn_output) * rknn_process->io_num.n_output);
    for (uint i = 0; i < rknn_process->io_num.n_output; i++) {
        rknn_process->outputs[i].index = i;
        rknn_process->outputs[i].want_float = 0; // 默认不需要
    }

    rknn_process->inputs[0].buf = img_fp16.data;
    ret = rknn_outputs_release(rknn_process->ctx, rknn_process->io_num.n_output, rknn_process->outputs);
    printf("1 %d\n", ret);
    ret = rknn_inputs_set(rknn_process->ctx, rknn_process->io_num.n_input, rknn_process->inputs);
    printf("2 %d\n", ret);
    ret = rknn_run(rknn_process->ctx, NULL);
    printf("3 %d\n", ret);
    ret = rknn_outputs_get(rknn_process->ctx, rknn_process->io_num.n_output, rknn_process->outputs, NULL);
    printf("4 %d\n", ret);
    

    FILE* fp = fopen("output.bin", "wb");
    fwrite(rknn_process->outputs[0].buf, 1, rknn_process->outputs[0].size, fp);
    fflush(fp);
    fclose(fp);

    fp = fopen("output.txt", "w");
    float16_t* start = (float16_t*)rknn_process->outputs[0].buf;
    for (int i = 0; i < 8400; i++) {
        float16_t conf = start[4 * 8400 + i];
        if (conf < 0.3) continue;
        for (int j = 0; j < 56; j++) {
            fprintf(fp, "%.5f ", start[j * 8400 + i]);
        }
        fprintf(fp, "\n");
    }
    fflush(fp);
    fclose(fp);
}

