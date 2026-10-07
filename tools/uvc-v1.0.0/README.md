# UVC 构建材料（唯一源码）

构建在 WSL 中执行：`python3 tools/uvc-v1.0.0/build_uvc.py --output /tmp/uvc-build-check`。
保留脚本中的 SDK 固定提交、外部板端库和交叉编译器；本次未重建或删除工具链。
Windows `调试/工具/仪器测试工具/payload` 保留唯一运行/打包文件，已逐文件核对与旧 UVC payload 相同。
`ORIGINAL_SHA256SUMS` 是迁移前清单，含 Windows payload 的校验值；当前构建材料用 `SHA256SUMS` 核对。
旧 README.txt 中 image 路径仅为历史记录，当前材料路径以本说明为准。
