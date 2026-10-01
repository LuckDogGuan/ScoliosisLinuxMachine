# 下位机工程工作规则

- 本工程的真实源码和 Git 位于 WSL2 `Luckfox-Ubuntu-22.04:/home/guanjx/luckfox/Scoliosis`。编辑、构建、版本归档及 Git 写操作均通过 WSL2 内部的 Linux 工具执行；Windows 只保留上位机、Python 调试工具、文档和只读证据副本。
- 修改前用 GitNexus 分析符号影响；提交前运行 `detect_changes()`。当前可用索引是与真实源码按哈希核对的 `ScoliosisFirmwareFinal` 只读副本，Windows UNC 上的原生数据库锁不可靠；不要把副本当源码修改。
- 保留已有未提交改动和待传视频，逐文件处理，不使用递归批量删除。
- 发布先编译、验证，执行 `scripts/package_image.sh` 创建新的 `image/版本日期/`；已经发布的版本目录保持不变。版本包包含程序、适配库、声音、示例配置、启动脚本、说明和 SHA-256。
- 实现及验证记录见 `CHANGELOG.md`；用户操作、实板日志和三仓库版本关系见 Windows 项目 `调试/软件架构/拍摄配对调试改造_实施与验收_2026-10-01.md` 与 `调试/记录/上下位机版本与进度_2026-10-01.md`。
