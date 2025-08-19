import cv2
import  numpy as np

a = np.load("dumps/after_imquantize_0_int8_1x352x640x3.npy")
b = np.load("dumps/before_imquantize_0_uint8_1x352x640x3.npy")

print(f"before[0, 0] = {b[0, 0, 0]}")
print(f"after[0, 0] = {a[0, 0, 0]}")

cv2.imwrite("before.jpg", b[0, :, :, ::-1])
a = a.astype(np.int32)
a += 128
a = a.astype(np.uint8)
cv2.imwrite("after.jpg", a[0, :, :, ::-1])
