# 保持你的调试环境变量
export GST_DEBUG=*:4,rknn:7,mpp:7,mppenc:7,mpph264enc:7,GST_TRACER:7
export GST_MPP_NO_RGA=0
export GST_TRACERS="memory"

gst-launch-1.0 -v \
  v4l2src device=/dev/video0 io-mode=mmap do-timestamp=true \
        ! 'video/x-raw,format=NV16,width=1920,height=1080' \
        ! mpph264enc rc-mode=cbr bps=10000000 gop=30 \
        ! h264parse config-interval=-1 \
        ! rtph264pay pt=96 \
        ! udpsink host=192.168.10.213 port=5000