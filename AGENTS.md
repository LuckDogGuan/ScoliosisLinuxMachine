# 下位机工程工作规则

- 本工程的真实源码和 Git 位于 WSL2 `Luckfox-Ubuntu-22.04:/home/guanjx/luckfox/Scoliosis`。编辑、构建、版本归档及 Git 写操作均通过 WSL2 内部的 Linux 工具执行；Windows 只保留上位机、Python 调试工具、文档和只读证据副本。
- 修改前用 GitNexus 分析符号影响；提交前运行 `detect_changes()`。使用与当前 HEAD/改动按哈希核对的临时只读镜像索引，Windows UNC 上的原生数据库锁不可靠；不要把副本当源码修改。
- 保留已有未提交改动和待传视频，逐文件处理，不使用递归批量删除。
- 发布先编译、验证，执行 `scripts/package_image.sh` 构建并封装新的 `releases/<版本>/`；正式目录只跟踪压缩包、BUILD.txt、VERSION.md、SHA256SUMS，payload 为校验后删除的临时内容。既有包及 tag 不重写。发布记录核对 source_commit（旧版 source_base_commit）、归档提交及 tag。UVC 构建源码唯一放在 `tools/uvc-v1.0.0/`；不恢复 image。
- 实现及验证记录见 `CHANGELOG.md`；用户操作、实板日志和三仓库版本关系见 Windows 项目 `调试/软件架构/拍摄配对调试改造_实施与验收_2026-10-01.md` 与 `调试/记录/上下位机版本与进度_2026-10-01.md`。
