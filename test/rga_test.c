// rga_letterbox_test.c
//
// Demo: single‑call Rockchip RGA letter‑box conversion
//        NV16 1920×1080  -> RGB888 640×640 (black border)
//
// Build  (on RK Linux SDK, make sure pkg‑config finds librga):
//   gcc rga_letterbox_test.c -o rga_letterbox_test $(pkg-config --cflags --libs
//   librga) # or: gcc rga_letterbox_test.c -o rga_letterbox_test -lRga -ldrm
//
// Run:
//   ./rga_letterbox_test input_1920x1080.nv16 output_640x640.rgb
//
// The program reads a raw NV16 frame (YUV422SP interleaved),
// calls RGA "improcess" once with IM_COLOR_FILL to do:
//   * NV16 → RGB888 colorspace convert
//   * 1920×1080 → 640×360 resize (keep AR)
//   * pad top & bottom to 640×640 with black (letter‑box)
// Then writes the raw RGB888 result.

#include <im2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main() {
  uint8_t *buf = malloc(16 * 16 * 3);
  int8_t* bufi = (int8_t*)buf;
  uint64_t* buf64 = (uint64_t*)buf;
  for (int i = 0; i < 256; i++) {
    buf[i * 3 + 0] = i;
    buf[i * 3 + 1] = i;
    buf[i * 3 + 2] = i;
  }
//   rga_buffer_t rga_buf = wrapbuffer_virtualaddr(buf, 16, 16, RK_FORMAT_RGB_888);
//   rga_buffer_t rga_buf2 = wrapbuffer_virtualaddr(buf2, 16, 16, RK_FORMAT_RGB_888);
//   im_nn_t nn_info;
//   nn_info.offset_b = -50;
//   nn_info.offset_g = -50;
//   nn_info.offset_r = -50;
//   nn_info.scale_b = 1 << 8;
//   nn_info.scale_g = 1 << 8;
//   nn_info.scale_r = 1 << 8;
//   imquantize(rga_buf, rga_buf2, nn_info);
  uint64_t x = 0x8080808080808080;
  for (int i = 0; i < (16 * 16 * 3 / 8); i++) {
    buf64[i] ^= x;
  }

  for (int i = 0; i < 256; i++) {
    printf("%d %d %d\n", bufi[i * 3 + 0], bufi[i * 3 + 1], bufi[i * 3 + 2]);
  }
}