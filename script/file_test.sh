export GST_DEBUG=3
export GST_MPP_NO_RGA=0
gst-launch-1.0 -e  v4l2src device=/dev/video0 io-mode=mmap do-timestamp=true\
      ! video/x-raw,format=NV16,width=1920,height=1080,framerate=30/1 \
      ! queue leaky=2 max-size-buffers=10 \
      ! mpph264enc rc-mode=cbr bps=4000000 \
                  gop=30 max-pending=2 qp-min=10 qp-max=30 profile=baseline  \
      ! h264parse ! mp4mux ! filesink location=test.mp4