# 注意，必须使用 rknn 专用的 ultralytics_yolo11
# cd ultralytics_yolo11 && pip install .
from ultralytics import YOLO
from rknn.api import RKNN
import numpy as np
import onnxruntime
import cv2
import os
import sys
# for sz in "nsmlx":
#imgsz = (352, 640)
# imgsz = (448, 800)
imgsz = (512, 960)
from rknn.utils import onnx_edit

onnx_outputs = [
        "/model.23/Sigmoid_output_0", # 1x1x4620，框的置信度
        "/model.23/Split_output_0", # 1x64x4620 框的坐标（需 dfl + 后处理）
        "/model.23/Sigmoid_1_output_0", # 1x17x4620 关键点的置信度
        "/model.23/Slice_2_output_0", # 1x34x4620 关键点的坐标（需后处理）
    ]

for sz in "l":
    for DEFAULT_QUANT in [True]:

        if isinstance(imgsz, int):
            imgsz = (imgsz, imgsz)
        file_path_prefix = f"model/yolo11{sz}-pose"
        pt_path = file_path_prefix + ".pt"
        onnx_path = file_path_prefix + f"_{imgsz[0]}_{imgsz[1]}" + ".onnx"
        optimized_onnx_path = file_path_prefix + f"_{imgsz[0]}_{imgsz[1]}" + "_opt.onnx"
        rknn_path = file_path_prefix + f"_{imgsz[0]}_{imgsz[1]}{'_quant' if DEFAULT_QUANT else ''}" + ".rknn"
        print(f"============ {rknn_path} ============")
        if not os.path.exists(onnx_path):
            yolo = YOLO(pt_path)
            os.rename(yolo.export(format="onnx", imgsz=imgsz), onnx_path)
        
        if not os.path.exists(rknn_path):
            DATASET_PATH = 'COCO/coco_subset_20.txt'
            rknn = RKNN(verbose=True, verbose_file=f"rknn_build_log_{sz}.txt")
            rknn.config(mean_values=[0, 0, 0], std_values=[255, 255, 255], target_platform="rk3588", 
                        quantized_algorithm="mmse", quantized_method="channel", single_core_mode=True)
            
            rknn.load_onnx(onnx_path, outputs=onnx_outputs)
            rknn.build(do_quantization=DEFAULT_QUANT, dataset=DATASET_PATH)
            rknn.export_rknn(rknn_path)
            
        img = cv2.imread('../test/test_640x352.jpg')
        img = img[..., ::-1]
        rknn = RKNN(verbose=True, verbose_file=f"rknn_build_log_{sz}.txt")
        rknn.config(mean_values=[0, 0, 0], std_values=[255, 255, 255], target_platform="rk3588", 
                    quantized_algorithm="mmse", quantized_method="channel", single_core_mode=True)
        rknn.load_rknn(rknn_path)
        rknn.init_runtime(target="rk3588")
        outputs = rknn.inference(inputs=[img])
        rknn.release()
        
        # 创建 dumps 目录（如果不存在）
        os.makedirs("./dumps_py", exist_ok=True)
        
        # 保存每个输出为 npy 文件
        for i, output in enumerate(outputs):
            # 构造 shape_str
            shape_str = "x".join(str(dim) for dim in output.shape)
            
            # 根据数据类型确定 dtype_str
            dtype_str = str(output.dtype)
            
            # 构造文件名，包含 dtype 和 shape 信息
            prefix = f"dumps_py/output_{i}"
            filename = f"{prefix}_{dtype_str}_{shape_str}.npy"
            
            # 保存为 npy 文件
            np.save(filename, output)
            print(f"Saved output {i} to {filename}")
        
