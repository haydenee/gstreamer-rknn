#ifndef _RKNN_PROCESS_H_
#define _RKNN_PROCESS_H_
#include "rknn_api.h"
#include "rknn_meta.h"
#include <glib/gasyncqueue.h>
#include <gst/gst.h>
#include <im2d.h>

struct RknnEngine {
  GstObject *filter;
  // queue
  GAsyncQueue *rknn_input_queue;
  GAsyncQueue *rknn_output_queue;
  // loading
  int worker_id;
  char *model_path;
  // rknn api
  rknn_context *worker_0_ctx;
  rknn_context ctx;
  rknn_input_output_num io_num;
  rknn_tensor_attr input_attr; 
  rknn_tensor_attr output_attrs[4];
  rknn_tensor_mem *input_mem;
  rknn_tensor_mem *output_mem; 
  rknn_tensor_mem *output_mems[4];
  rga_buffer_t rga_input_buffer;
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
  struct RknnMeta *rknn_meta;
};

#ifdef __cplusplus
extern "C" {
#endif
int rknn_init_process(struct RknnEngine *rknn_process);
int rknn_preprocess(struct RknnEngine *engine, GstBuffer *raw_buffer);
int rknn_inference(struct RknnEngine *rknn_process);
int rknn_postprocess(struct RknnEngine *rknn_process, float box_conf_threshold,
                     float nms_threshold);
void rknn_visualize(struct RknnEngine *rknn_process, GstBuffer *output_buffer);
void rknn_dump_io(struct RknnEngine *rknn_process);
void rknn_release(struct RknnEngine *rknn_process);
void dump_tensor_data(const char *prefix, uint index,
                      const rknn_tensor_attr *attr, const void *data,
                      size_t size);
#ifdef __cplusplus
}
#endif

#endif