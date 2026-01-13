export GST_DEBUG=3
export GST_MPP_NO_RGA=0
SOCKET_CONFIG_PATH="${SOCKET_CONFIG_PATH:-socket_config.json}"

gst-launch-1.0 -e \
      v4l2src device=/dev/video0 io-mode=dmabuf do-timestamp=true num-buffers=300 \
      ! videorate drop-only=true \
      ! video/x-raw,format=NV16,width=1920,height=1080,framerate=30/1 \
      ! rknn workers=3 \
          model-path=/root/gstreamer-rknn/model/yolo11l-pose_352_640_quant.rknn \
          draw-boxes=true \
          resize-mode=crop \
          mppjpegdec-offset-workaround=false \
          socket-config-path=${SOCKET_CONFIG_PATH} \
      ! mpph264enc rc-mode=cbr bps=4000000 gop=30 max-pending=2 qp-min=10 qp-max=30 profile=baseline \
      ! h264parse \
      ! mp4mux \
      ! filesink location=test.mp4
