# Repository Guidelines

本仓库提供基于 RK3588 的 GStreamer RKNN 插件（当前以 YOLOv5 为主），使用 Meson/Ninja 构建。

## 语言与沟通
- 所有与用户的交互、文档、说明及注释均使用中文。

## 项目结构与模块组织
- `src/` 插件核心代码（`gstrknn.*`、`rknnprocess.*`、`rgaprocess.*`、`socket_utils.*`）。
- `test/` 测试可执行文件源码与样例素材（jpg/raw/mp4）。
- `script/` 测试与演示脚本，主要入口为 `remote_test.sh`。
- `model/` RKNN 模型；`asset/` 图片与说明；`thirdparty/` 依赖库回退。
- `build/`、`build-aarch64/` 为构建产物目录。
- `docker/` 交叉编译相关文件；当前已在容器内开发时可忽略。

## 构建、测试与开发命令
- `meson setup build` 初始化构建目录。
- `ninja -C build` 编译插件与测试程序。
- `ninja -C build install` 安装 `libgstrknn.so` 到 GStreamer 插件目录。
- `gst-inspect-1.0 rknn` 验证插件可发现性。
- `./build/rgatest`、`./build/dmabuftest` 运行 C 测试。
- `script/remote_test.sh` 端到端验证入口脚本。

## 编码风格与命名约定
- 遵循 GStreamer 约定：`gst_plugin_rknn_*` 函数、`GST_*` 宏、C++ 类使用 CamelCase（如 `SocketClient`）。
- 缩进遵循文件现有风格：核心文件多为 2 空格，`socket_utils.*` 多为 4 空格，避免全局格式化。
- 文件名使用 snake_case，并带模块前缀（如 `rknnprocess`、`socket_utils`）。

## 测试规范
- 无单元测试框架，优先使用 `test/` 可执行文件与 `script/remote_test.sh` 做回归验证。
- 多数测试依赖 RK3588 与 RKNN Runtime；变更推理或管线时记录 fps/延迟或输出文件。

## 提交与 Pull Request 规范
- 提交信息多使用 Conventional Commits（可带 scope），如 `feat(socket): add connection retry mechanism`。
- PR 需包含变更摘要、硬件/系统信息、模型路径、完整 `gst-launch-1.0` 命令及关键日志/产出。
- 若修改 `socket_config.json`、`model/` 或 `thirdparty/`，请在 PR 中明确说明。

## 配置与运行注意事项
- `model-path` 与 `label-path` 必填，避免硬编码路径。
- `socket_config.json` 控制 socket 行为，协议变更需同步更新配置与说明。
- `librga` 与 `librknnrt` 在 Meson 配置时从系统或 `thirdparty/` 回退加载。
