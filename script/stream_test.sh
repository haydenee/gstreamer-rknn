export GST_DEBUG=*:3,rknn:6
export GST_MPP_NO_RGA=0
test-launch "( \
  v4l2src device=/dev/video0 io-mode=mmap do-timestamp=true \
        ! video/x-raw,format=NV16,width=1920,height=1080\
        ! queue max-size-buffers=2 leaky=downstream \
        ! rknn silent=true bypass=false model-path=/root/gstreamer-rknn/model/yolov5s-640-640.rknn\
        ! mpph264enc rc-mode=cbr bps=10000000 gop=30 level=4.2 profile=baseline\
        ! rtph264pay name=pay0 pt=96 \ )"
# This code works!