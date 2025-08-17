#ifndef _RKNN_APP_DTYPE_CONVERT_H_
#define _RKNN_APP_DTYPE_CONVERT_H_

#include "rknn_api.h"
#include "type_half.h"


int get_type_size(rknn_tensor_type type){
    switch (type){
        case RKNN_TENSOR_INT8:
            return sizeof(int8_t);
        case RKNN_TENSOR_UINT8:
            return sizeof(uint8_t);
        case RKNN_TENSOR_INT16:
            return sizeof(int16_t);
        case RKNN_TENSOR_UINT16:
            return sizeof(uint16_t);
        case RKNN_TENSOR_INT32:
            return sizeof(int32_t);
        case RKNN_TENSOR_UINT32:
            return sizeof(uint32_t);
        case RKNN_TENSOR_INT64:
            return sizeof(int64_t);
        case RKNN_TENSOR_FLOAT16:
            return sizeof(half);
        case RKNN_TENSOR_FLOAT32:
            return sizeof(float);
        default:
            printf("    get_type_size error: not support dtype %d\n", type);
            return 0;
    }
}


int rknn_app_dtype_convert(unsigned char* src_ptr, 
                         rknn_tensor_type src_dtype,
                         unsigned char* dst_ptr,
                         rknn_tensor_type dst_dtype, 
                         int n_elems, float scale, int zero_point, bool verbose = false)
{    
    int type_size = get_type_size(dst_dtype);

    if (src_dtype == dst_dtype){
        // for keep same function logic, still create new buffer.
        memcpy(dst_ptr, src_ptr, n_elems * type_size);
        return 0;
    }

    if (verbose){
        printf("    rknn_dtype_convert: convert from %s to %s\n", get_type_string(src_dtype), get_type_string(dst_dtype));
    }
    int convert_success = 0;
    switch (src_dtype){
        case RKNN_TENSOR_FLOAT32:
            if (dst_dtype == RKNN_TENSOR_FLOAT16){
                float_to_half_array((float*)src_ptr, (half*)dst_ptr, n_elems);

            } else if (dst_dtype == RKNN_TENSOR_INT8){
                for (int i = 0; i < n_elems; i++){
                    ((int8_t*)dst_ptr)[i] = (int8_t)(((float*)src_ptr)[i] / scale + zero_point);
                }
            } else if (dst_dtype == RKNN_TENSOR_INT16){
                for (int i = 0; i < n_elems; i++){
                    ((int16_t*)dst_ptr)[i] = (int16_t)(((float*)src_ptr)[i] / scale + zero_point);
                }
            } else if (dst_dtype == RKNN_TENSOR_INT32){
                for (int i = 0; i < n_elems; i++){
                    ((int32_t*)dst_ptr)[i] = (int32_t)(((float*)src_ptr)[i]);
                }
            } else if (dst_dtype == RKNN_TENSOR_INT64){
                for (int i = 0; i < n_elems; i++){
                    ((int64_t*)dst_ptr)[i] = (int64_t)(((float*)src_ptr)[i]);
                }
            } else {
                convert_success = -1;
            }
            break;

        case RKNN_TENSOR_FLOAT16:
            if (dst_dtype == RKNN_TENSOR_FLOAT32){
                half_to_float_array((half*)src_ptr, (float*)dst_ptr, n_elems);
            } else {
                convert_success = -1;
            }
            break;

        case RKNN_TENSOR_INT8:
            if (dst_dtype == RKNN_TENSOR_FLOAT32){
                for (int i = 0; i < n_elems; i++){
                    ((float*)dst_ptr)[i] = (float)(((int8_t*)src_ptr)[i] - zero_point) * scale;
                }
            } else {
                convert_success = -1;
            }
            break;

        case RKNN_TENSOR_INT16:
            if (dst_dtype == RKNN_TENSOR_FLOAT32){
                for (int i = 0; i < n_elems; i++){
                    ((float*)dst_ptr)[i] = (float)(((int16_t*)src_ptr)[i] - zero_point) * scale;
                }
            } else {
                convert_success = -1;
            }
            break;

        case RKNN_TENSOR_INT32:
            if (dst_dtype == RKNN_TENSOR_FLOAT32){
                for (int i = 0; i < n_elems; i++){
                    ((float*)dst_ptr)[i] = (float)(((int32_t*)src_ptr)[i]);
                }
            } else {
                convert_success = -1;
            }
            break;

        case RKNN_TENSOR_INT64:
            if (dst_dtype == RKNN_TENSOR_FLOAT32){
                for (int i = 0; i < n_elems; i++){
                    ((float*)dst_ptr)[i] = (float)(((int64_t*)src_ptr)[i]);
                }
            } else {
                convert_success = -1;
            }
            break;

        default:
            convert_success = -1;
            break;
    }
    
    if (convert_success == -1){
        printf("    rknn_dtype_convert: not support dtype convert from %s to %s\n", get_type_string(src_dtype), get_type_string(dst_dtype));
    }

    return convert_success;
}


#endif 