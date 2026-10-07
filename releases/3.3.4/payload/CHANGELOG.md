## 3.3.4 — 2026-10-07

- 不足3000 ms的录像判为short；设备端上传授权拒绝short/failed及不足3000 ms，原始短视频保留，不删除。
- Flutter同时拦截不足3秒上传，将旧固件2–3秒success事件正常确认成short并安排重拍。
- 设备测试增加0–100背光亮度，GPIO55/PWM11_M1（ff490030）硬件PWM 1 kHz；保存到state/backlight.json，诊断退出/租约到期关闭，后续拍摄使用保存亮度。
- 调试JPEG质量70→95、缓冲与传输上限4 MiB，保留1280×1080原始相机分辨率，Flutter放大预览并使用高质量缩放。
- 固件和RKiPC显式TZ=CST-8；安装/etc/TZ（纳入安装备份），UTC Unix时间戳与RTC不加8小时，文件名/OSD/Flutter显示东八区。
- 回归：实际completed/begin_upload的1999/2000/2999/3000边界；UDP亮度范围/身份/租约/持久化；0–100占空比；原GPIO时序/配对灯；Flutter上传授权/完成事件/UTC+8测试。
- 实板与发布记录见Windows调试/记录/capture-fixes-20261007。物理亮度与画质、按键长短录像、断电验收按实板记录区分，不由主机测试推定。

# 3.3.3 - Capture backlight and integrated hardware setup (2026-10-07)

- Fix missing business backlight: allowed left+right hold 500 ms enables GPIO55; releasing either key delays off 1000 ms. Rehold cancels off; invalid sample/gap/permission loss and graceful exit fail closed.
- Existing 1500 ms capture trigger, pairing PWM and audio remain; app supplies permission, middleware owns timer, platform owns GPIO/PWM and lock.
- Compile validated GPIO/pull-up and configfs PWM setup into platform adapters. No deployed button_inputs.sh/light_pwm.sh/S79buttons; installer backs up and retires exact obsolete paths. Keep init/install/Wi-Fi deployment files.
- Ignore regenerable validation products; bundle checks use temporary workspaces with automatic cleanup. Keep historical acceptance README and immutable old releases.
- ARM and host timing/sysfs checks run; visual physical timing and cold-power boot require operator acceptance.

# 3.3.2 - Pairing status indication (2026-10-07)

- Normal center LED steady; actual open pairing window breathes through shared PWM2 helpers.
- Physical middle long press toggles the unpaired window; deadline 45 seconds, clears stale candidate transaction and queues one exit announcement.
- Pair success restores steady with existing success audio. Add spoken entry deadline and exit confirmation; audio playback unchanged.
- Graceful service shutdown turns LED off and releases owned GPIO32/PWM/overlay; exclusive diagnostics require stopping business service.
- Host regression covers physical key deadline/cancel/commit/expiry plus actual sysfs duty transitions; visual and acoustic acceptance remain pending.

# 3.3.1 - Standalone light diagnostic (2026-10-07)

- Add bounded --light-test before business initialization: center off/on/slow/fast/breathe and manual backlight.
- Use Linux sysfs, flock and installed configfs/DT compiler; only transient PWM2_M0. Keep light_update, pairing, capture and three-key policy unchanged.
- GPIO32 return requires operator verification; unknown/occupied resources fail explicitly.
- Normal timeout, INT/TERM/HUP and handled errors clean outputs; SIGKILL/power loss need reboot/manual recovery.
- Formal releases/<version>, complete payload, immutable archive, source commit and annotated tag.
- Host mode/timing/signal/fault tests and existing upload/pending/discard/buttons checks passed. Visual/electrical and cold-power acceptance remain manual. See docs/light-test.md.

# 3.3.0 - Capture deletion while permission remains enabled (2026-10-02)

- Proof-bound durable discard intent, stop only the matched upload with SIGKILL and reap it before cleanup; gate and unrelated recording remain active.
- Pending lists, upload grants, completion events and maintenance hash writes reject tombstoned capture IDs; startup resumes unfinished per-file discards.
- Host regression covers live uploader ignoring SIGTERM, enabled gate, repeated receipt, mismatched proof, no reappearance in pending list and crash recovery.
- Flutter counterpart adds capture-page deletion, school browsing/class gate, fixed-ruler test inference and workspace progress. Firmware sources/builds/archives/Git remain in WSL2.
- Base before this task: 3a3ac12 plus previously uncommitted 3.2.2 records changes, preserved in this commit. Board rollback binary: validation/capture-results-20261002/board-before.instrument_app; board copy /opt/instrument/backups/capture-results-20261002/instrument_app.

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

## 2026-10-04 部署默认网络与音量、独立 SSH 更新

- 默认配置网络 MinMax-test、PC 地址 192.168.101.2:8080、音量 20；部署包携带默认 WPA 模板。
- 安装器仅初始化缺失或出厂占位 Wi-Fi，保留已有真实网络；同步出厂 /etc 入口以避免重启覆盖 /userdata，纳入安装备份和文件校验。
- 最终包 v3.3.0-20261004-minmax-volume20-bundle2；ARM/uClibc 编译、配置校验、包测试通过。
- 实板 41eda5f2c5b00de1 完成 ADB 首装、停止 PC ADB 后的独立路由器 SSH 更新和重启恢复，配置/ALSA 音量 20；另一台序列号不匹配被只读拒绝。
- 证据：Windows 调试/记录/adb-ssh-update-20261004/部署与独立SSH升级验收.md；相机传感器仍未识别，不属于本次部署通道验收通过范围。
