## 3.2.2 - 2026-10-02 UTC+8

- Isolate discard work from maintenance to prevent old event delivery starving proof-bound cleanup.

## 3.2.1 - 2026-10-02 UTC+8

- Add paired-host capture_discard with capture ID, transaction, exact size and SHA256.
- Hash and cleanup run in a worker; current recording/upload is rejected. Durable per-capture intent supports retries, and success is returned only after cleanup completes.
- Keep pairing, config, pending media, events and student associations through deployment/rollback.
- Validation: ARM build, host regressions and proof-bound discard tests; see Windows records-flow-20261002 evidence for live acceptance.

## 3.2.0 (2026-10-02)

- Delay failure recovery during initial Wi-Fi setup for 60 seconds; log state and recovery action correlation. Real disconnect recovery is retained.
- Add paired-host UTC synchronization and RTC write, rejecting recording/finalization.
- Add renewable diagnostics lease, raw button state and bounded JPEG preview worker. Diagnostics suppress pairing and capture keys; normal operation requires all keys released.
- Enable RKiPC JPEG channel in persistent startup, leave periodic snapshots disabled.
- Firmware source/build/package/Git writes remain in WSL2; retain config, pairing state, pending videos and event receipts during rollout/rollback.

# 下位机修改与进度

## 3.1.0 / 2026-10-01

本次将此前未提交的 V3、LwBTN 基线和当前已验证改造一起保存为可复现版本；旧 Git 基点为 `82e16b4`，改造前真实源码及板端自报 0.0.1 的程序分别在 `image/baseline-20261001/` 保存。

| 项目 | 修改前 | 修改后/进度 |
|---|---|---|
| 配对 | 中＋左/中＋右；普通发现长期开放 | 中键 3 秒开放 60 秒窗口或解除，原配对身份支持恢复；已实现和自动验证 |
| 拍摄 | 学生查看与任务绑定耦合、历史状态导致锁定 | 上位机明确准备原 ID；录制中解除先收尾，关闭下一轮；已验证 |
| 音量 | 上位机缓存、V3 无设置与试听 | 硬件应用、持久确认、临时试听及恢复；保存失败和实板应用已验证，听感待现场 |
| 上传 | 短片/未知跳过；旧事件阻塞重试 | 自动上传、幂等回执与单文件删除、旧事件隔离；已验证 |
| 录像 | 3 段约少 1 秒片头 | mux 初始化后请求本板支持的 IDR；20 段正常＋1 短片解码/哈希通过，最大偏差 32 ms |
| 分层 | 硬件、协议、业务混在原文件 | app / middleware / platform，单一 V3 状态机；已实现 |
| 调试 | 两端日志不易关联 | Python 原包 UDP＋流式 HTTP，单调/UTC 与各 ID 日志轮转；实链路通过 |
| 版本 | 有未提交代码与分散编译文件 | WSL2 源码、image 编译包、Git 提交和发布标签；本轮归档 |

C `make check-host`、42 项协议检查及 ARM/uClibc 编译通过；Flutter 45 项测试、静态分析与 Windows Release 通过。20 轮和边界实板、电脑重开、板端重启自动启动已通过。实体按键手感、仪器听感、断电冷启动和镜头内计时画面仍待现场。

旧记录中的原因不明重启/解除，用户已确认属于人工操作。保留原日志，不将其标记为电气故障。业务部署继续使用 `/opt/instrument`，回滚时保留待传视频、事件和上位机新增关联记录。
