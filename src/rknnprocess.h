#ifndef _RKNN_PROCESS_H_
#define _RKNN_PROCESS_H_
#include "rknn_api.h"


#ifdef __cplusplus
extern "C" {
#endif

int rknn_prepare(const char* model_name, rknn_context ctx, rknn_input* input, rknn_input_output_num* io_num, int* model_width, int* model_height, int* model_channel);

#ifdef __cplusplus
}
#endif

#endif