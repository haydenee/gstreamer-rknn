
It is only written for rk3588 + yolo v5

When building the pipeline with RK3588 hdmi rx, ensure v4l2src output format is native hdmi input format, otherwise there is high conversion cost in v4l2src.

Performance:
3820x2160: 19 fps
1920x1080: 30 fps, latency ~150 ms 



Reference:
[rknn_yolov5_demo](https://github.com/airockchip/rknn-toolkit2/tree/master/rknpu2/examples/rknn_yolov5_demo)
