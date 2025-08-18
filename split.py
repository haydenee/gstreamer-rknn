import cv2
cap = cv2.VideoCapture('test.mov')
frameId = 0
while True:
    print(frameId)
    ret, frame = cap.read()
    if not ret:
        break
    print(frame.shape)
    path = f"test/test_1/{frameId:06d}.png"
    cv2.imwrite(path, frame)
    frameId += 1    