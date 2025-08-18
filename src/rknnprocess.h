#ifndef _RKNN_PROCESS_H_
#define _RKNN_PROCESS_H_
#include "rknn_api.h"
#include <gst/gst.h>

#define MAX_PERSON 128

struct RknnMeta {
  GstClockTime start_time;
  GstClockTime preprocess_time;
  GstClockTime queue_time;
  GstClockTime infer_time;
  GstClockTime postprocess_time;
  GstClockTime visualize_time;
  GstClockTime end_time;

  float results[MAX_PERSON][56];
  int results_size;
};
struct RknnEngine {
  // loading
  int worker_id;
  unsigned char *model_data;
  char *model_path;
  // rknn api
  rknn_context ctx;
  rknn_input *inputs;
  rknn_output *outputs;
  rknn_input_output_num io_num;
  rknn_tensor_attr *input_attrs;
  rknn_tensor_attr *output_attrs;
  // shapes
  int model_width;
  int model_height;
  int img_width;
  int img_height;
  int model_channel;
  // scailing and padding
  int model_pad_left;
  int model_pad_top;
  int model_pad_right;
  int model_pad_bottom;
  int img_pad_left;
  int img_pad_top;
  int img_pad_right;
  int img_pad_bottom;
  float scale_img_to_model;
  float scale_model_to_img;
  // results
  float results[MAX_PERSON][56];
  int results_size;
};



#ifdef __cplusplus
extern "C" {
#endif

int rknn_prepare(struct RknnEngine *rknn_process, rknn_context *shared_context, int core);
int rknn_inference(struct RknnEngine *rknn_process);

int rknn_postprocess(struct RknnEngine *rknn_process, float box_conf_threshold,
                     float nms_threshold);

void rknn_visualize(struct RknnEngine *rknn_process, GstBuffer *output_buffer);
void rknn_dump_io(struct RknnEngine *rknn_process);
void rknn_release(struct RknnEngine *rknn_process);

#ifdef __cplusplus
}
#endif

#endif