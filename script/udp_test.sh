export GST_DEBUG=*:4
# export GST_DEBUG=*:3,rknn:7,mpp:7,mppenc:7,mpph264enc:7,GST_TRACER:7
export GST_MPP_NO_RGA=0
export GST_TRACERS="leaks"
gst-launch-1.0 -v \
  v4l2src device=/dev/video0 io-mode=mmap do-timestamp=true \
        ! video/x-raw,format=NV16\
        ! rknn silent=true bypass=false show-fps=true model-path=../model/yolov5s-640-640.rknn label-path=../model/coco_80_labels_list.txt\
        ! mpph264enc rc-mode=cbr bps=10000000 gop=30 \
        ! h264parse config-interval=-1 \
        ! rtph264pay pt=96 \
        ! udpsink host=192.168.10.213 port=5000

        # ! queue max-size-buffers=5 leaky=downstream \
