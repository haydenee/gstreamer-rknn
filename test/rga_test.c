// rga_letterbox_test.c
//
// Demo: single‑call Rockchip RGA letter‑box conversion
//        NV16 1920×1080  -> RGB888 640×640 (black border)
//
// Build  (on RK Linux SDK, make sure pkg‑config finds librga):
//   gcc rga_letterbox_test.c -o rga_letterbox_test $(pkg-config --cflags --libs librga)
//   # or: gcc rga_letterbox_test.c -o rga_letterbox_test -lRga -ldrm
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

#include <errno.h>
#include <fcntl.h>
#include <im2d_type.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "RgaUtils.h" // wrapbuffer_* helpers (part of librga dev package)
#include "im2d.h" // Rockchip librga  im2d/high‑level API
#include "rga.h"
#define SRC_W 1920
#define SRC_H 1080
#define DST_W 640
#define DST_H 640

static void die(const char* msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input_nv16> <output_rgb>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* in_path = argv[1];
    const char* out_path = argv[2];

    size_t src_size = SRC_W * SRC_H * 2; // NV16 = 2 bytes/pixel
    size_t dst_size = DST_W * DST_H * 3; // RGB888 = 3 bytes/pixel

    // Allocate user memory buffers (for quick functional test).
    void* src_buf = aligned_alloc(16, src_size);
    void* dst_buf = aligned_alloc(16, dst_size);
    if (!src_buf || !dst_buf)
        die("malloc");
    // fill dst_buf with random data (for testing)
    // for (size_t i = 0; i < dst_size; i++) {
    //     ((unsigned char*)dst_buf)[i] = rand() % 256; // random byte
    // }
    // Load NV16 raw frame
    FILE* fin = fopen(in_path, "rb");
    if (!fin)
        die("fopen input");
    if (fread(src_buf, 1, src_size, fin) != src_size)
        die("read input");
    fclose(fin);


    // Prepare rga_buffer_t for source & destination (virtual addr variant).
    rga_buffer_t src = wrapbuffer_virtualaddr_t(src_buf, SRC_W, SRC_H, SRC_W, SRC_H, RK_FORMAT_YCbCr_422_SP);
    rga_buffer_t dst = wrapbuffer_virtualaddr_t(dst_buf, DST_W, DST_H, DST_W, DST_H, RK_FORMAT_RGB_888);
    rga_buffer_t pat = wrapbuffer_virtualaddr_t(NULL, 0, 0, 0, 0, RK_FORMAT_RGB_888); // pattern unused
    // Define rectangles
    im_rect srect = { 0, 0, SRC_W, SRC_H };
    // Scaled height keeping AR: 640 * 1080 / 1920 = 360 → pad 140 px top/bottom.
    im_rect drect = { 0, 140, DST_W, 360 };
    im_rect prect = { 0, 0, 0, 0 }; // pattern rect unused

    // improcess options
    im_opt_t opt;
    memset(&opt, 0, sizeof(opt));
    opt.color = 0xaa0000; // black background fill color (ARGB)

    int usage = IM_SYNC; // enable background fill

    int ret = improcess(src, dst, // src, dst buffers
        pat, // pattern buffer unused
        srect, drect, prect,
       usage );

    if (ret != IM_STATUS_SUCCESS) {
        fprintf(stderr, "improcess failed: %s\n", imStrError(ret));
        return EXIT_FAILURE;
    }

    // Save RGB result as BMP (24-bit)
    FILE* fout = fopen(out_path, "wb");
    if (!fout)
        die("fopen output");

    // BMP header sizes
    int row_stride = DST_W * 3;
    int pad = (4 - (row_stride % 4)) % 4; // BMP rows are padded to 4 bytes
    int bmp_data_size = (row_stride + pad) * DST_H;
    int file_size = 54 + bmp_data_size;

    // BMP file header (14 bytes)
    unsigned char bmp_file_header[14] = {
        'B', 'M',                  // Signature
        file_size, file_size >> 8, file_size >> 16, file_size >> 24, // File size
        0, 0, 0, 0,                // Reserved
        54, 0, 0, 0                // Pixel data offset
    };

    // BMP info header (40 bytes)
    unsigned char bmp_info_header[40] = {
        40, 0, 0, 0,               // Header size
        DST_W, DST_W >> 8, DST_W >> 16, DST_W >> 24, // Width
        DST_H, DST_H >> 8, DST_H >> 16, DST_H >> 24, // Height
        1, 0,                      // Planes
        24, 0,                     // Bits per pixel
        0, 0, 0, 0,                // Compression (none)
        bmp_data_size, bmp_data_size >> 8, bmp_data_size >> 16, bmp_data_size >> 24, // Image size
        0, 0, 0, 0,                // X pixels per meter
        0, 0, 0, 0,                // Y pixels per meter
        0, 0, 0, 0,                // Colors used
        0, 0, 0, 0                 // Important colors
    };

    fwrite(bmp_file_header, 1, 14, fout);
    fwrite(bmp_info_header, 1, 40, fout);

    // Write pixel data (bottom-up, BGR, padded)
    unsigned char pad_bytes[3] = {0, 0, 0};
    for (int y = DST_H - 1; y >= 0; y--) {
        unsigned char* row = (unsigned char*)dst_buf + y * DST_W * 3;
        // Convert RGB to BGR for BMP
        for (int x = 0; x < DST_W; x++) {
            unsigned char bgr[3] = { row[x*3+2], row[x*3+1], row[x*3+0] };
            fwrite(bgr, 1, 3, fout);
        }
        fwrite(pad_bytes, 1, pad, fout);
    }
    fclose(fout);

    printf("Letter‑box conversion done → %s (BMP format)\n", out_path);

    free(src_buf);
    free(dst_buf);
    return EXIT_SUCCESS;
}
