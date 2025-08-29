REMOTE_HOST="root@opi-003.local"
REMOTE_DIR="/root/gstreamer-rknn"
LOG_FILE="log"
MP4_FILE="test.mp4"
TEST_SCRIPT="./script/image_stream_test.sh"
rsync -avz --exclude='.git' --exclude='docker' --exclude='build*' --exclude='log' --exclude='*.bmp' --exclude='*.mp4' ./ $REMOTE_HOST:$REMOTE_DIR/
