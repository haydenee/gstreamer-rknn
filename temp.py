import cv2
import  numpy as np

for i in range(5):
    
    a = np.load(f"dumps/preprocess_{i}_int8_1x352x640x3.npy")

    # a = a.astype(np.int32)
    # a += 128
    a = a.astype(np.uint8)
    cv2.imwrite(f"after_{i}.jpg", a[0, :, :, ::-1])