# 固定安装脊柱尺暗影读数核心

源码唯一在本目录。纯 C11，复用仓库 cJSON，不依赖 OpenCV、ONNX 或相机驱动。`ruler_reader` 不开相机、不改变正式录像。

`make all arm` 构建 Linux 和 RV1106/uClibc 程序；Windows 在 Flutter 仓库运行 `tool/build_shadow_reader.ps1`，直接读取本目录的 UNC 源文件，输出 Windows 适配程序和源码哈希。

## 输入与输出

- `ruler_reader calibration.json input.pgm`：单张精确尺寸 P5 灰度图；PGM 不允许注释或多余尾部数据。
- `ruler_reader calibration.json --stream`：常驻进程，每帧恰好 width×height 字节、行优先 8 位灰度；每张输入对应一行 `ANGLE_RESULT_JSON`。此接口只计算，不声称包含拍摄时钟。
- `ruler_reader calibration.json --benchmark input.pgm 300`：重复测核心耗时；不包含取帧、解码、传输和显示。

JSON 保留 `angle_deg` 兼容字段，读取失败为 null，同时返回 reason、point、area、processing_ms、sequence、algorithm_version、calibration_version。正负由实际标定决定；绝不推高接近5度的测量值。

标定 schema=1：精确 width/height、背景灰度 background、可读亮带 mask_indices、固定安装 anchor_indices、带 usable 标记的位置/角度 nodes。无效节点之间不插值、不外推。先用稳定区域拟合曝光，再检查安装残差，定位亮带内最大暗影；多个暗影、暗影不足、离轨或超范围均拒绝。

## 当前边界

现有640×480人工标签照片仅有-6～+10度的可用节点；这不是经过独立验证的量程或精度。正式1280×1080视频不可自动缩放后套用该标定。`label_status=manual_unverified`固定表示当前探索版本；另行验证前不可升级为已验证标定。

板端程序只完成可复用计算核和隔离测速；RKiPC原始帧出口、正式计算worker、拍摄ID/PTS关联、结果可靠补传需在同安装原始尺寸标定通过后接入，禁止使用诊断JPEG轮询替代6Hz取流。现有instrument_app未改变。

## 回退与测试

本目录不链接instrument_app，不改变其版本和包；移除集成入口即可回到原ONNX流程。输出程序在忽略的build目录；不建立Windows固件源码镜像。

`python3 test_reader.py --reader <absolute reader> --profile <calibration.json> --pgm <6-degree fixture.pgm>`验证持久进程、失败空值、错误标定、截断及多暗影；同样的测试可在Windows运行。
