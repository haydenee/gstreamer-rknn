#!/bin/bash

# 设置调试级别 - 使用最高级别verbosity
export GST_DEBUG=*:3,rknn:7
export GST_MPP_NO_RGA=0

# 使用multifilesrc从test/bus.jpg创建30fps的视频流
# 每帧都使用相同的图片，模拟30fps视频流
# 图片实际分辨率为810x1080，需要缩放到640x640以适应yolov5s模型
# 使用num-buffers=100限制为100帧后自动停止
gst-launch-1.0 -e \
  multifilesrc location=test/bus.jpg loop=true num-buffers=100 \
    ! image/jpeg,width=810,height=1080,framerate=30/1 \
    ! jpegdec \
    ! videoconvert \
    ! videoscale \
    ! video/x-raw,format=NV16,width=640,height=640,framerate=30/1 \
    ! queue max-size-buffers=2 leaky=downstream \
    ! rknn silent=false bypass=false \
        model-path=../model/yolov5s-640-640.rknn \
        label-path=../model/coco_80_labels_list.txt \
    ! mpph264enc rc-mode=cbr bps=2000000 gop=30 level=4.2 profile=baseline \
    ! h264parse \
    ! mp4mux \
    ! filesink location=test/bus_detection_output.mp4

# 如果需要实时流输出，可以使用下面的命令（取消注释）
# gst-launch-1.0 -v \
#   multifilesrc location=test/bus.jpg loop=true \
#     ! image/jpeg,width=810,height=1080,framerate=30/1 \
#     ! jpegdec \
#     ! videoconvert \
#     ! videoscale \
#     ! video/x-raw,format=NV16,width=640,height=640,framerate=30/1 \
#     ! queue max-size-buffers=2 leaky=downstream \
#     ! rknn silent=false bypass=false \
#         model-path=../model/yolov5s-640-640.rknn \
#         label-path=../model/coco_80_labels_list.txt \
#     ! mpph264enc rc-mode=cbr bps=2000000 gop=30 level=4.2 profile=baseline \
#     ! rtph264pay pt=96 \
#     ! udpsink host=127.0.0.1 port=5000