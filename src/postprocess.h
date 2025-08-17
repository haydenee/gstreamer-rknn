#pragma once
#include <array>
#include <vector>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif 
struct QuantArray {
  int8_t* data;
  int32_t zp;
  float scale;
};
struct Rect {
  float x;
  float y;
  float w;
  float h;
};

struct Box {
  int index;
  Rect rect;
  float conf;
};

struct Person {
  Box box;
  std::array<float, 51> kpts;
};

struct ResultData {
  
  int height;
  int width;
  int count;
  QuantArray boxes_confs;
  QuantArray boxes;
  QuantArray kpts_confs;
  QuantArray kpts;
  std::vector<Box> filtered_boxes;
  std::vector<Person> results;
  float* boxes_exp;
};

void fill_exp_table(float* exp_table, int zp, float scale);
void post_process(float conf_threshold, float nms_threshold, ResultData* data);

#ifdef __cplusplus
}
#endif