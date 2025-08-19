#!/bin/bash
unset GST_V4L2_USE_LIBV4L2
echo "=== inspect ==="
echo "rknn:"

eval "PAGER=cat gst-inspect-1.0 rknn"

echo "=== 创建测试图片序列 ==="
# 创建多个软连接到同一张图片来模拟多帧视频
rm -f /tmp/frame_*.jpg
for i in {0..30}; do
    ln -s /root/gstreamer-rknn/test/test.jpg /tmp/frame_$(printf "%02d" $i).jpg
done

echo "=== cmd ==="
CMD="gst-launch-1.0 -e \
    multifilesrc location=/tmp/frame_%02d.jpg index=0 caps=\"image/jpeg, framerate=30/1\" \
    ! jpegparse \
    ! mppjpegdec dma-feature=true \
    ! videorate \
    ! video/x-raw, framerate=30/1 \
    ! rknn workers=1 \
        model-path=/root/gstreamer-rknn/model/yolo11l-pose_352_640_quant.rknn \
        draw_boxes=true \
        resize_mode=crop \
        mppjpegdec_offset_workaround=1 \
    ! mpph264enc rc-mode=cbr bps=4000000 gop=30 max-pending=2 qp-min=10 qp-max=30 profile=baseline  \
    ! h264parse ! mp4mux ! filesink location=test.mp4"

# 
# CMD="gst-launch-1.0 -e \
#     v4l2src device=/dev/video0 io-mode=dmabuf do-timestamp=true num-buffers=3000 \
#     ! videorate drop-only=true \
#     ! video/x-raw,framerate=30/1 \
#     ! rknn workers=3 \
#         model-path=/root/gstreamer-rknn/model/yolo11l-pose_352_640_quant.rknn \
#         draw_boxes=true \
#         resize_mode=crop \
#         mppjpegdec_offset_workaround=0 \
#     ! mpph264enc rc-mode=cbr bps=4000000 gop=30 max-pending=2 qp-min=10 qp-max=30 profile=baseline  \
#     ! h264parse ! mp4mux faststart=true ! filesink location=test.mp4"

# 使用 test2.mov 文件输入
# CMD="gst-launch-1.0 -e \
#     filesrc location=test2.mov \
#     ! parsebin \
#     ! mppvideodec \
#     ! videorate \
#     ! rknn workers=3 \
#         model-path=/root/gstreamer-rknn/model/yolo11l-pose_352_640_quant.rknn \
#         draw_boxes=true \
#         resize_mode=crop \
#         mppjpegdec_offset_workaround=0 \
#     ! mpph264enc rc-mode=cbr bps=4000000 gop=30 max-pending=2 qp-min=10 qp-max=30 profile=baseline  \
#     ! h264parse ! mp4mux faststart=true ! filesink location=test.mp4"


echo "$CMD"
# GST_DEBUG Levels:                                  |
# |---|---------|----------------------------------------------------------------|
# | 0 | none    | No debug information is output.                                |
# | 1 | ERROR   | Logs all fatal errors. These are errors that do not allow the  |
# |   |         | core or elements to perform the requested action. The          |
# |   |         | application can still recover if programmed to handle the      |
# |   |         | conditions that triggered the error.                           |
# | 2 | WARNING | Logs all warnings. Typically these are non-fatal, but          |
# |   |         | user-visible problems are expected to happen.                  |
# | 3 | FIXME   | Logs all "fixme" messages. Those typically that a codepath that|
# |   |         | is known to be incomplete has been triggered. It may work in   |
# |   |         | most cases, but may cause problems in specific instances.      |
# | 4 | INFO    | Logs all informational messages. These are typically used for  |
# |   |         | events in the system that only happen once, or are important   |
# |   |         | and rare enough to be logged at this level.                    |
# | 5 | DEBUG   | Logs all debug messages. These are general debug messages for  |
# |   |         | events that happen only a limited number of times during an    |
# |   |         | object's lifetime; these include setup, teardown, change of    |
# |   |         | parameters, etc.                                               |
# | 6 | LOG     | Logs all log messages. These are messages for events that      |
# |   |         | happen repeatedly during an object's lifetime; these include   |
# |   |         | streaming and steady-state conditions. This is used for log    |
# |   |         | messages that happen on every buffer in an element for example.|
# | 7 | TRACE   | Logs all trace messages. Those are message that happen very    |
# |   |         | very often. This is for example is each time the reference     |
# |   |         | count of a GstMiniObject, such as a GstBuffer or GstEvent, is  |
# |   |         | modified.                                                      |
# | 9 | MEMDUMP | Logs all memory dump messages. This is the heaviest logging and|
# |   |         | may include dumping the content of blocks of memory.           |
# +------------------------------------------------------------------------------+
#export GST_DEBUG=*:1,rknn:7,mpp:7,mppdec:7,mpph264dec:7:exif-tags:1
export GST_DEBUG=*:1,rknn:4

export GST_MPP_NO_RGA=0

# The Linux supports enabling/disabling HAL log printing by setting environment variables (librga 1.9.0 and above):

# enable log print：
# export ROCKCHIP_RGA_LOG=1
# set log level：

# The log level is divided into full print (0), DEFAULT (1), DEBUG (3), INFO (4), WRANING (5), ERROR (6).
export ROCKCHIP_RGA_LOG=0
export ROCKCHIP_RGA_LOG_LEVEL=5

export mpp_log_level=7 # silence 
echo "=== envs ==="
env | grep -iE 'GST|MPP|RGA'

echo "=== output ==="
eval "$CMD"

echo "=== 清理临时文件 ==="
rm -f /tmp/frame_*.jpg