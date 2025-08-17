#!/bin/bash
unset GST_V4L2_USE_LIBV4L2
echo "=== inspect ==="
echo "rknn:"
eval "gst-inspect-1.0 rknn"

echo "=== 创建测试图片序列 ==="
# 创建多个软连接到同一张图片来模拟多帧视频
rm -f /tmp/frame_*.jpg
for i in {0..29}; do
    ln -s /root/gstreamer-rknn/test/test.jpg /tmp/frame_$(printf "%02d" $i).jpg
done

echo "=== cmd ==="
CMD="gst-launch-1.0 -e \
    multifilesrc location=/tmp/frame_%02d.jpg index=0 caps=\"image/jpeg, framerate=10/1\" \
    ! jpegparse \
    ! mppjpegdec dma-feature=true \
    ! videorate \
    ! video/x-raw \
    ! rknn workers=1 \
        model-path=/root/gstreamer-rknn/model/yolo11l-pose_352_640_quant.rknn \
        resize_mode=crop \
        mppjpegdec_offset_workaround=1 \
    ! mpph264enc rc-mode=cbr bps=4000000 gop=30 max-pending=2 qp-min=10 qp-max=30 profile=baseline  \
    ! h264parse ! mp4mux ! filesink location=test.mp4"


echo "$CMD"

#export GST_DEBUG=*:1,rknn:3,mpp:7,mppenc:7,mpph264enc:7:exif-tags:1
export GST_DEBUG=*:0,rknn:1
export GST_MPP_NO_RGA=1
export ROCKCHIP_RGA_LOG=0
export mpp_log_level=7
echo "=== envs ==="
env | grep GST

echo "=== output ==="
eval "$CMD"

echo "=== 清理临时文件 ==="
rm -f /tmp/frame_*.jpg