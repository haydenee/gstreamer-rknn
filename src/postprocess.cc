#include "postprocess.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <gst/gst.h>

GST_DEBUG_CATEGORY_EXTERN(gst_plugin_rknn_debug);
#define GST_CAT_DEFAULT gst_plugin_rknn_debug

struct GridPos {
  int stride;
  int i;
  int j;
};

inline float sigmoid(float x) { return 1.0 / (1.0 + expf(-x)); }

inline float unsigmoid(float y) { return -1.0 * logf((1.0 / y) - 1.0); }

inline int32_t clip_float(float val, float min, float max) {
  float f = val <= min ? min : (val >= max ? max : val);
  return f;
}

inline int8_t quantize_f32_to_affine(float f32, int32_t zp, float scale) {
  float dst_val = (f32 / scale) + zp;
  int8_t res = (int8_t)clip_float(dst_val, -128, 127);
  return res;
}

inline float dequantize_affine_i8_to_f32(int8_t qnt, int32_t zp, float scale) {
  return ((float)qnt - (float)zp) * scale;
}

void fill_exp_table(float *exp_table, int32_t zp, float scale) {
  GST_DEBUG("fill exp table zp %d scale %f", zp, scale);
  for (int i = 0; i < 256; i++) {
    int8_t val = static_cast<int8_t>(i - 128);
    exp_table[i] = exp(dequantize_affine_i8_to_f32(val, zp, scale));
    //GST_TRACE("%d -> %f -> %f\n", (int)val, dequantize_affine_i8_to_f32(val, zp, scale), exp_table[i]);
  }
}

Rect compute_box_from_dfl(int8_t *data, GridPos pos, float *exp_table) {
  constexpr int DFL_LEN = 16;

  std::array<float, 4> ret;

  for (int b = 0; b < 4; b++) {
    float exp_sum = 0;
    float acc_sum = 0;

    int8_t *arr = data + b * DFL_LEN;
    for (int idx = 0; idx < DFL_LEN; idx++) {
      exp_sum += exp_table[arr[idx] + 128];
    }
    for (int idx = 0; idx < DFL_LEN; idx++) {
      acc_sum += exp_table[arr[idx] + 128] / exp_sum * idx;
    }

    // 临时存一下
    ret[b] = acc_sum;
  }

  float x1, y1, x2, y2, w, h;
  x1 = (-ret[0] + pos.j + 0.5) * pos.stride;
  y1 = (-ret[1] + pos.i + 0.5) * pos.stride;
  x2 = (ret[2] + pos.j + 0.5) * pos.stride;
  y2 = (ret[3] + pos.i + 0.5) * pos.stride;
  w = x2 - x1;
  h = y2 - y1;

  return Rect{x1, y1, w, h};
}

GridPos grid_pos_from_index(int idx, ResultData *data) {
  GridPos ret;
  int area = data->width * data->height;
  int sizes[3] = {area / 8 / 8, area / 16 / 16, area / 32 / 32};
  int offset;
  int stride;
  if (idx < sizes[0]) {
    offset = 0;
    stride = 8;
  } else if (idx < sizes[0] + sizes[1]) {
    offset = sizes[0];
    stride = 16;
  } else {
    offset = sizes[0] + sizes[1];
    stride = 32;
  }
  int w = data->width / stride;
  idx -= offset;
  ret.i = idx / w;
  ret.j = idx % w;
  ret.stride = stride;
  return ret;
}

float calculate_overlap(const Rect &rect0, const Rect &rect1) {
  float xmin0 = rect0.x;
  float ymin0 = rect0.y;
  float xmax0 = rect0.x + rect0.w;
  float ymax0 = rect0.y + rect0.h;

  float xmin1 = rect1.x;
  float ymin1 = rect1.y;
  float xmax1 = rect1.x + rect1.w;
  float ymax1 = rect1.y + rect1.h;

  float w = fmax(0.f, fmin(xmax0, xmax1) - fmax(xmin0, xmin1) + 1.0);
  float h = fmax(0.f, fmin(ymax0, ymax1) - fmax(ymin0, ymin1) + 1.0);
  float i = w * h;
  float u = (xmax0 - xmin0 + 1.0) * (ymax0 - ymin0 + 1.0) +
            (xmax1 - xmin1 + 1.0) * (ymax1 - ymin1 + 1.0) - i;
  return u <= 0.f ? 0.f : (i / u);
}

void post_process(float conf_threshold, float nms_threshold, ResultData *data) {

  GST_DEBUG("post_process start conf_threshold %f nms_threshold %f", conf_threshold, nms_threshold);
  // prefill exp table

  GST_DEBUG("post_process boxes_exp_filled");
  // filter by conf
  data->filtered_boxes.clear();
  int8_t conf_i8 = quantize_f32_to_affine(conf_threshold, data->boxes_confs.zp,
                                          data->boxes_confs.scale);
  for (int k = 0; k < data->count; k++) {
    int8_t conf_i8_cur = data->boxes_confs.data[k];
    float conf_cur = dequantize_affine_i8_to_f32(
        conf_i8_cur, data->boxes_confs.zp, data->boxes_confs.scale);
    // GST_DEBUG("k = %d, conf_i8 = %d, conf_i8_cur = %d conf_cur = %f", k, conf_i8, conf_i8_cur, conf_cur);

    if (conf_i8_cur >= conf_i8) {
      int8_t dfl_data[64];
      GridPos pos = grid_pos_from_index(k, data);
      for (int i = 0; i < 64; i++) {
        dfl_data[i] = data->boxes.data[i * data->count + k];
      }
      // dfl: 16 -> 1 
      Rect rect = compute_box_from_dfl(dfl_data, pos, data->boxes_exp);
      Box box{k, rect, conf_cur};
      data->filtered_boxes.emplace_back(box);
    }
  }

  GST_DEBUG("filtered_boxes size: %zu", data->filtered_boxes.size());
  // sort by conf
  std::sort(data->filtered_boxes.begin(), data->filtered_boxes.end(),
            [](const Box &a, const Box &b) { return a.conf > b.conf; });

  // nms
  data->results.clear();
  for (auto &box : data->filtered_boxes) {
    bool keep = true;
    for (auto &prev_person : data->results) {
      float iou = calculate_overlap(box.rect, prev_person.box.rect);
      if (iou > nms_threshold) {
        keep = false;
        break;
      }
    }
    if (keep) {
      data->results.emplace_back();
      auto &person = data->results.back();
      person.box = box;
      int index = box.index;
      GridPos pos = grid_pos_from_index(index, data);
      for (int j = 0; j < 17; j++) {
        int x_idx = j * 2 * data->count + 0 * data->count +
                    index; // kpts.data: 1x17x2xcount
        int y_idx = j * 2 * data->count + 1 * data->count + index;
        int conf_idx = j * data->count + index; // kpts_confs.data: 1x17x1xcount
        float x = dequantize_affine_i8_to_f32(data->kpts.data[x_idx],
                                              data->kpts.zp, data->kpts.scale);
        float y = dequantize_affine_i8_to_f32(data->kpts.data[y_idx],
                                              data->kpts.zp, data->kpts.scale);
        person.kpts[j * 3 + 0] = (x * 2 + pos.j) * pos.stride;
        person.kpts[j * 3 + 1] = (y * 2 + pos.i) * pos.stride;
        person.kpts[j * 3 + 2] = dequantize_affine_i8_to_f32(
            data->kpts_confs.data[conf_idx], data->kpts_confs.zp,
            data->kpts_confs.scale);
        // printf("index %3d i %3d j %3d stride %2d joint %2d x_i8 %4d x %6.3f "
        //        "y_i8 %4d y %6.3f c_i8 %4d c %6.3f\n",
        //        index, pos.i, pos.j, pos.stride, j, data->kpts.data[x_idx], x,
        //        data->kpts.data[y_idx], y, data->kpts_confs.data[conf_idx],
        //        person.kpts[j * 3 + 2]);
      }
    }
  }

  GST_DEBUG("finished post processing: %zu persons", data->results.size());
}