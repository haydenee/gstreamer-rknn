#ifndef _DATA_UTILS_H_
#define _DATA_UTILS_H_

#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "cnpy.h"
using namespace cnpy;

#include "path_utils.h"
#include "resize_function.h"


static unsigned char *load_image_and_autoresize(const char *image_path, LETTER_BOX* letter_box, rknn_tensor_attr *input_attr)
{
    // printf("loading %s\n", image_path);
    int req_height = 0;
    int req_width = 0;
    int req_channel = 0;

    switch (input_attr->fmt)
    {
    case RKNN_TENSOR_NHWC:
        req_height = input_attr->dims[1];
        req_width = input_attr->dims[2];
        req_channel = input_attr->dims[3];
        break;
    case RKNN_TENSOR_NCHW:
        req_height = input_attr->dims[2];
        req_width = input_attr->dims[3];
        req_channel = input_attr->dims[1];
        break;
    default:
        printf("meet unsupported layout\n");
        return NULL;
    }

    int height = 0;
    int width = 0;
    int channel = 0;

    unsigned char *image_data = stbi_load(image_path, &width, &height, &channel, req_channel);
    if (image_data == NULL)
    {
        printf("load image-%s failed!\n", image_path);
        return NULL;
    }

    if (channel == 1){
        printf("image is grey, convert to RGB");
        void* rgb_data = malloc(width* height* 3);
        for(int i=0; i<height; i++){
            for(int j=0; j<width; j++){
                    int offset = (i*width + j)*3;
                    ((unsigned char*)rgb_data)[offset] = ((unsigned char*)image_data)[offset];
                    ((unsigned char*)rgb_data)[offset + 1] = ((unsigned char*)image_data)[offset];
                    ((unsigned char*)rgb_data)[offset + 2] = ((unsigned char*)image_data)[offset];
            }
        }
        free(image_data);
        image_data = (unsigned char*)rgb_data;
        channel = 3;
    }

    letter_box->in_width = width;
    letter_box->in_height = height;
    letter_box->channel = channel;

    if (width != req_width || height != req_height || channel != req_channel)
    {
        printf("image shape match failed!, (%d, %d, %d) != (%d, %d, %d)\n", height, width, channel, req_height, req_width, req_channel);
        printf("Trying resize image\n");        

        letter_box->target_height = req_height;
        letter_box->target_width = req_width;

        unsigned char *resize_buf = (unsigned char *)malloc(letter_box->target_height* letter_box->target_width* letter_box->channel);
        if (resize_buf == NULL){
            printf("malloc resize_buf failed!\n");
            return NULL;
        }

        compute_letter_box(letter_box);
        stb_letter_box_resize(image_data, resize_buf, *letter_box);
        letter_box->reverse_available = true;
        free(image_data);
        image_data = resize_buf;
    }

    return image_data;
}


static unsigned char* load_npy(const char* input_path, rknn_tensor_attr* input_attr, rknn_tensor_type* data_type){
    NpyArray npy_data = npy_load(input_path);
    int npy_type_bytes = npy_data.word_size;
    std::string typeName = npy_data.typeName;
    // printf("npy data type:%s\n", typeName.c_str());

    unsigned char* data = (unsigned char*)malloc(npy_data.num_bytes());
    if (!data){
        printf("  malloc failed!\n");
        return NULL;
    }
    if (input_attr == NULL){
        memcpy(data, npy_data.data<unsigned char>(), npy_data.num_bytes());
        return data;
    }

    char shape_str[512];
    memset(shape_str, 0, 512);
    for (int i=0; i<npy_data.shape.size(); i++){
        sprintf(shape_str, "%s%lu,", shape_str, npy_data.shape[i]);
    }

    // check dim size
    if (npy_data.shape.size() != input_attr->n_dims){
        printf("  npy shape size not match, %lu != %d\n", npy_data.shape.size(), input_attr->n_dims);
        printf("  npy data shape is: [%s]\n", shape_str);
        dump_tensor_attr(input_attr);
        return NULL;
    }

    // npy file should save in nchw format.(shape should be same as onnx/pt model)
    // check shape match
    int npy_target_dims[input_attr->n_dims];
    int channel_dim = 0;
    if (input_attr->fmt == RKNN_TENSOR_NHWC){
        npy_target_dims[0] = input_attr->dims[0];
        npy_target_dims[1] = input_attr->dims[3];
        npy_target_dims[2] = input_attr->dims[1];
        npy_target_dims[3] = input_attr->dims[2];
        channel_dim = npy_target_dims[1];
    }
    else{
        for (int i=0; i<input_attr->n_dims; i++){
            npy_target_dims[i] = input_attr->dims[i];
        }
    }
    for (int i=0; i<input_attr->n_dims; i++){
        if (npy_data.shape[i] != npy_target_dims[i]){
            printf("  npy shape[%d] not match, %lu != %d\n", i, npy_data.shape[i], npy_target_dims[i]);
            printf("  npy data shape is: [%s]\n", shape_str);
            dump_tensor_attr(input_attr);
            return NULL;
        }
    }

    // check data type
    if (data_type != NULL){
        if (typeName == "float32") {
            *data_type = RKNN_TENSOR_FLOAT32;
        } else if (typeName == "float16") {
            *data_type = RKNN_TENSOR_FLOAT16;
        } else if (typeName == "int32") {
            *data_type = RKNN_TENSOR_INT32;
        } else if (typeName == "int64") {
            *data_type = RKNN_TENSOR_INT64;
        } else if (typeName == "int8") {
            *data_type = RKNN_TENSOR_INT8;
        } else if (typeName == "uint8") {
            *data_type = RKNN_TENSOR_UINT8;
        } else if (typeName == "int16") {
            *data_type = RKNN_TENSOR_INT16;
        } else if (typeName == "uint16") {
            *data_type = RKNN_TENSOR_UINT16;
        } else if (typeName == "uint32") {
            *data_type = RKNN_TENSOR_UINT32;
        } else if (typeName == "bool") {
            *data_type = RKNN_TENSOR_BOOL;
        } else if (typeName == "int4") {
            *data_type = RKNN_TENSOR_INT4;
        } else {
            printf("  meet unsupported type:%s\n", typeName.c_str());
            return NULL;
        }
    }

    memcpy(data, npy_data.data<unsigned char>(), npy_data.num_bytes());
    return data;
}

inline int load_npy_shape(const char* input_path, rknn_tensor_attr* input_attr){
    NpyArray npy_data = npy_load(input_path);
    for (int i=0; i<npy_data.shape.size(); i++){
        input_attr->dims[i] = npy_data.shape[i];   
    }
    return 0;
}


inline int load_image_shape(const char* image_path, rknn_tensor_attr* input_attr){
    int height = 0;
    int width = 0;
    int channel = 0;

    unsigned char *image_data = stbi_load(image_path, &width, &height, &channel, 3);
    if (image_data == NULL)
    {
        printf("load image-%s failed!\n", image_path);
        return -1;
    }

    input_attr->dims[0] = 1;
    input_attr->dims[1] = height;
    input_attr->dims[2] = width;
    input_attr->dims[3] = channel;
    input_attr->n_dims = 4;
    input_attr->fmt = RKNN_TENSOR_NHWC;

    free(image_data);
    return 0;
}


static void save_npy(const char* output_path, float* data, rknn_tensor_attr* attr)
{
    std::vector<size_t> shape;

    for (uint32_t i = 0; i < attr->n_dims; ++i) {
        shape.push_back(attr->dims[i]);
    }
    npy_save<float>(output_path, data, shape);
}


inline float compute_cosine_similarity(float* a, float* b, int len){
    float dot = 0.0, denom_a = 0.0, denom_b = 0.0 ;
    for (int i = 0; i < len; i++){
        dot += a[i] * b[i];
        denom_a += a[i] * a[i];
        denom_b += b[i] * b[i];
    }
    return dot / (sqrt(denom_a) * sqrt(denom_b));
}


inline float compare_npy_cos_similarity(const char* fpath_1, const char* fpath_2, int len){
    void* data_1 = load_npy(fpath_1, NULL, NULL);
    void* data_2 = load_npy(fpath_2, NULL, NULL);

    float cos = compute_cosine_similarity((float*)data_1, (float*)data_2, len);
    free(data_1);
    free(data_2);
    return cos;
}


#endif