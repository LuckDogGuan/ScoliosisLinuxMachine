# 3.4.1 — 2026-10-26 上下位机收尾补丁对齐

- 与上位机3.4.1同步补丁号，中间版本号保持4。上位机补充运行期间的DISPLAY1窗口边界限制及隐藏窗口实测，避免启动后的移动/DPI更新占用屏幕2。
- 仪器业务、通信及诊断逻辑不变；3.4.0已通过真实Wi-Fi断网补采，包和分阶段证据保留。本版重新构建、安装并验证当前版本和文件映射。
- 当前组合与最终验收见docs/release-index.md。

# 3.4.0 — 2026-10-26 上下位机运行诊断正式收尾

- 按用户要求提升中间版本号，与上位机3.4.0统一收尾；保留3.3.33候选及3.3.34/35/36正式包、标签和分阶段验收记录。
- 收录被动故障采集、独立有界日志、同编号离线补采、明确拒绝原因、忙碌去误报、事件/协议双序号及状态查询实时版本回读。
- 3.3.36与上位机3.3.42已通过真实Wi-Fi射频关闭20秒、离线导出、重连补采、HTTP接收中断、导入合并与协议验收；本版只升级收尾版本号，仍重做包和部署校验。
- Windows界面调试仅允许DISPLAY1，绝不占用DISPLAY2。最终部署与验收见docs/release-index.md和Windows调试仓库版本索引。

# 3.3.36 — 2026-10-26 诊断忙碌去误报与日志序号关联

- evidence_collect的evidence_busy仍记录协议拒绝，但不生成新故障标记，避免被动采集自己制造故障；错误主机等真实拒绝仍触发故障。
- 日志sequence保留为进程事件递增序号，协议报文序号另记protocol_sequence，不互相覆盖。
- V3 hello_reply随状态查询返回当前firmware，上位机直接更新版本，避免配对后长期沿用旧广播版本。
- 配套上位机3.3.42保留并导出协议序号，修正工作空间嵌套日志的最后成功步骤显示。3.3.35与上位机3.3.41的真实Wi-Fi射频关闭20秒、离线导出和同编号重连补采已通过并保留独立证据。
- 继承3.3.35的明确分页错误及对应版本说明自动生成；本版安装及实板验收结果另记docs/release-index.md，不覆盖旧包。

# 3.3.35 — 2026-10-26 Wi-Fi 日志联调与版本对应

- 待传查询的非法分页返回 invalid_pending_query；读取待传目录失败返回 pending_storage_unavailable，不再把两类故障都记为 precondition_not_met。每页仍最多8条，授权和待传材料保留规则不变。
- 配套上位机3.3.41修正删除核对的16条分页，并在一次空闲清理中把历史文字日志恢复到预算内，使新事件立即恢复保存。
- 打包时 VERSION.md 从本版本 CHANGELOG 条目生成，缺少对应条目即停止；正式版本目录和既有包不覆盖。
- 3.3.33为早期候选，3.3.34增加稳定进程运行ID后正式归档。3.3.34已于本日通过Wi-Fi安装和两端真实采集、接收中断补采、导出/导入、身份及旧启动ID拒绝验收。
- 构建、安装和各项验收的对应索引见 docs/release-index.md；3.3.35实板结果在完成后追加，不以构建代替。

# 3.3.34 — 2026-10-26 故障定位与诊断导出

- 进程运行ID绑定系统启动、PID与首次单调时间，避免调用点传入不同boot字段时分裂同一进程。
- 运行日志迁到独立 `/opt/instrument/logs/`；兼容采集旧 state 日志。
- 单条事件2KiB、有界启动输出、重复计数与丢失计数；异常留下小型故障标记。
- 被动 evidence_v1 采集及查询复用UDP37020与现有PC HTTP；重任务仅空闲运行，拍摄优先，不占硬件诊断租约。
- 故障材料512KiB/次、8份共4MiB、最长7天；支持保存的材料再次补采。详细记录最多10分钟。
- 自动化和ARM构建与实板验收分开，本次不安装或操作仪器。

# 3.3.33 — 2026-10-26 故障诊断候选（被3.3.34替代）

- 源码721c54d：首次加入被动故障证据采集。配套Windows3.3.39；仅构建候选，未安装实板，未作为正式归档/tag。
- 后续3957cb0修正进程运行ID稳定性，正式归档为3.3.34。候选包保留本地，不覆盖也不冒充当前版本。

# 3.3.32 - 检测状态播报与班级完成（2026-10-23）

- 检测许可开启/暂停分别播报“检测已就绪”“检测已暂停”；单次采集正常结束播“检测完成”，过短和失败改为检测用语。
- 已配对且最近15秒内与本电脑通信、设备空闲、未开启检测时，左右键持续按住1.5秒提示“请先在电脑端开启检测”；同次长按只提示一次，两次触发至少间隔20秒。许可开启立即清除过时提醒，不放宽原采集门禁或改变按键节奏。
- capture_disable新增可选reason：round_completed/class_completed/skip_student关闭许可时静音，手动暂停与现有故障收口仍提示暂停；关闭许可不打断已进入的录像。
- 新增detection_status_voice_v1能力及已配对主机专属detection_class_complete，空闲且许可关闭时播“本班检测已完成”；operation_id去重，忙碌/未授权拒绝。
- 上位机3.3.36提供独立“完成本班检测”确认按钮，按完整班级名单检查成功采集，跳过和筛选隐藏的人员不算完成；名单自然走完仅播“本轮检测已结束”。
- 视频上传成功继续静音；语音焦点、音频工作进程、失败不阻断检测、上传回执及待传数据保持原行为。构建与真实设备验收分开记录。

# 3.3.31 - 检测语音调整（2026-10-23）

- 开始提示音改为“开始检测”，沿用当前 Microsoft Kangkang 音色、rate=-1、16kHz 单声道 16bit PCM。
- V3 和旧版上传成功后均不再播报；保留回执校验、上传失败提示和文件处理行为。
- 提示音可由 scripts/generate_detection_prompt.ps1 重建；主机验证与设备实际更新分别记录。

# 3.3.30（2026-10-22）

- 录像命令成功后确认本次封装器确实收到成功视频帧且唯一新文件开始写入，再报告实际录制；不将零帧的回执当作正在录像。
- 仅在尚未报告录制、仍按住原按键、无任何新增候选文件且停止已确认时，恢复原预览并重新应用拍摄参数，受控重试一次。持续无帧、多候选、损坏文件仍返回失败，保留原文件。
- 停止后有界等待唯一完整MP4收尾，再恢复预览；记录分阶段错误码、候选数量和路径。默认640×480、H.265、30帧及日期层关闭保持。
- 新增真实录像生命周期故障注入及封装器代际/首帧校验回归。实板部署、编码尺寸及多轮录像证据单独记录，不以主机桩测试代替现场验收。

# 3.3.29（2026-10-22）

- 启动RKiPC前关闭`type=dateTime`的OSD层，保留其他OSD、隐私遮罩、原始录像和文件/事件时间。取消日期不作为算法精度修复。
- 新增`camera_settings_v1`能力和配对主机专属`camera_get_settings`/`camera_set_settings`。只暴露当前V4L2 NV12尺寸范围与编码通道上限都允许的候选；枚举不可用时列表为空，不编造支持。
- 默认拍摄640×480、H.265/MP4、60/2配置；可保存驱动支持的其他调试尺寸至独立camera-profile.json，不改网络/配对配置。配对后的默认640由新版上位机下发。帧率/码率/GOP不新增未经验证的选项。
- 配置在录像前应用并要求RPC回读；结束/失败仍恢复原预览参数。忙碌、非配对主机、错误诊断令牌、非整数和不支持尺寸拒绝；暂时无法枚举驱动不抹掉已保存选择。
- 新增只读ARM能力探针`tools/camera_profile_probe.c`，不启动录像、不改参数。
- ARM/host编译、真实UDP模拟驱动的支持过滤/持久化/重启/忙碌检查、日期层隔离测试及640切换/回读/恢复回归通过。实板固件部署和无日期实拍验收另行记录，不以编译代替。

## 3.3.28 - 640x480 recording (2026-10-21)

- Switch the encoder to 640x480 before recording, verify RPC readback, and restore the original preview resolution afterwards. Keep 30 fps, H.265/MP4 and 4000 kbps.
- Default to 640x480; accept installed 1280x1080 configs and normalize recording metadata in memory without rewriting network/pairing configuration.
- The Windows signed ruler model still requires 1280x1080; 640x480 model adaptation and physical board acceptance are pending. Deployment and encoded-frame acceptance are recorded separately.

## 3.3.27 — capture-bound backlight snapshot (2026-10-20)

- Apply the configured percent at the recording authorization handshake, check GPIO/PWM control readback, and bind it to that capture. Control readback is not optical illumination proof.
- Persist the snapshot in recording/completion journals and per-video metadata; return it in completion events, pending-query responses and upload headers. Restart recovery reads the original capture snapshot; legacy files remain unknown.
- Host regression covers immutable snapshot persistence, pending/recovery, minimum duration and recording watchdogs. User confirmed no independent angle labels; the Windows 65 candidate is not promoted by this firmware release.

# Unreleased - Windows formal-video R4 C DLL (2026-10-16)

- Add an independent C11 signed-reference core under tools/ruler-signed; retain the existing preview reader and all firmware services unchanged.
- Export versioned load/analyze-gray/analyze-PNG/free ABI, bounded parameter loading and Windows system-WIC PNG input without Python/OpenCV runtime dependencies.
- Preserve the frozen round-four right-edge/local-shadow algorithm, float32 reductions, exact grayscale rule and explicit invalid results. Windows Flutter owns the serialized background FFI worker and model package.
- Full and four-fold reference parity, Windows runtime/packaging, timing and Python-free dependency evidence are recorded in the Windows Luckfox ruler-native-r4-20261016 audit. Linux ASan/UBSan boundary tests passed. No board deployment or firmware version bump.

# 3.3.15 - restore existing event voice policy (2026-10-16)

- Remove the global queue_sound whitelist introduced in f94bea2 (3.3.12). Existing pairing, capture-enable/disable, upload and storage event prompts can enter the queue again; do not silently redefine the agreed voice policy during timing optimizations.
- The PC "start capture from this position" action already sends capture_enable. Restoring its capture_enabled prompt requires no PC changes or duplicate new prompt.
- Retain the eight-entry capacity guard, independent playback worker, speech handover/preemption, idempotent RPC replies and capture continuation on audio failure. No WAV/volume or camera configuration changes.
- Regression coverage checks all 22 existing assets, actual UDP pair/enable/renew/disable/unpair prompts, no repeated speech for duplicate/unauthorized RPCs, and capture rhythm with restored event prompts.
- Hardware deployment and observable playback evidence are recorded separately.

# 3.3.14 - restore essential device prompts (2026-10-16)

- Restore boot_ready, wifi_connected, wifi_disconnected and wifi_restored through the shared prompt whitelist; retain the existing WAV assets and volume.
- Announce disconnect only when a previously available network becomes unavailable. Offline startup, repeated recovery polls and PC unpair do not create extra disconnect announcements.
- Keep prompt playback in its existing independent worker; preserve capture priority, PC speech leases and capture continuation on playback failure.
- Host startup/network transition regressions, audio-isolation/failure/speech-focus checks and ARM build passed. Formal 3.3.14 package and board acceptance are recorded separately; physical audible acceptance is not implied by host tests.
- Details: validation/startup-network-prompts-20261015.md.

# 3.3.13 - Wi-Fi camera diagnostics (2026-10-15)

- Normalize RKiPC storage mount to / at startup, matching VIDEO_DIR=/video0 and the capture spool filesystem.
- Add diagnostics_health_v1 capability and round/boot/build-correlated stage/error/file/upload evidence.
- Keep prior failures during background retries; cancel ended-round workers and bound diagnosis to 7.5 seconds.
- Report storage path mismatch, snapshot RPC, JPEG completion and HTTP failures without touching the key loop.
- Preserve original recordings and pairing; live Wi-Fi deployment and acceptance evidence is in the Windows Luckfox record.

# Unreleased — hardware preflight (2026-10-15)

- Add read-only sensor-driver recognition, RKiPC process and status-RPC checks to the existing diagnostics worker; publish health before preview acquisition and include age/preview status in authenticated diagnostic replies.
- Expose GPIO55/PWM11 control readback; explicitly report lamp-presence as not observable. Host-test output never claims physical hardware success. No automatic restart or detection-triggered lamp changes.
- Keep diagnostic leases, identity checks, saved brightness and independent key sampling. ARM build and host fixture/UDP/backlight/stability/input-isolation/latency tests passed; physical disconnect, optics and cold-boot acceptance remain pending. No package, version bump, deployment or tag in this change.
- Details: validation/hardware-preflight-20261015.md; Windows evidence: Luckfox/调试/记录/hardware-preflight-20261015/.

# 3.3.12 (2026-10-14)

- Keep 500 ms backlight and 1000 ms release-off delay. Reserve PC speech handover at 800 ms; announce capture once at 1000 ms; request recording from 1200 ms only while the authorized dual-button gesture remains valid.
- Advertise speech_preempt and expose read-only cancellation state. Device capture prompts take priority over PC names; normal upload, connection and capture-gate transitions remain in UI.
- Speech handover gap reduced to 200 ms. Final duration_ms remains the recording worker result shared by completion and pending-file metadata.
- Host boundary/lease/cancellation tests and ARM build passed. Real button-to-frame and audible timing acceptance must be recorded separately; no physical acceptance is implied by host tests.

# 3.3.11 — 2026-10-13

- 背光只根据独立按键采样有效性和拍摄许可保护关灯，避免 GPIO/PWM 同步操作导致主循环超过 100ms 后误熄灭。保留500ms点亮、松键1000ms后灭和真实采样失效保护。
- 设备测试回复增加 saved_brightness，brightness 保留当前测试输出；进入测试灭灯，调节后保存，退出不覆盖配置。补充退出重进和进程重启回读测试。
- ARM/主机回归及实板证据见Windows调试记录 backlight-school-export-20261013；实际光学与断电验收单独记载。

# 3.3.9 — 2026-10-13

- 业务进程异常退出后，未完成的拍摄日志按失败恢复，缺失 duration/result 不再默认为录像过短；重启初始化先禁止下一轮，后台确认相机 daemon 已停止后解除故障状态。保留事件、原片及6秒许可语义。
- 增加启动时相机仍在录像的独立回归，3.3.8正式包及回退保留；背光实现保持原样。

# 3.3.8 — 2026-10-13

- 拍摄不等待提示音，缺文件、播放失败或语音超时不阻止合法开录；启动初始化后排队播报“设备已启动”。统一 Huihui 正常语速、16kHz 单声道 PCM 提示，文件哈希与时长见 sounds/stability-voice-manifest.json。
- 外部命令、录像/上传/通知/诊断/维护任务有限等待；进程组回收。录像异常后关闭许可，仅在相机停止状态确认后恢复接收下一轮。
- V3 查询增加身份绑定的拍摄快照、单调时长、许可剩余和最近完成事件；保留持久化、重发、匹配回执后删除原片的机制。
- 完成事件扫描移到后台，按键采样独立运行，保持无效采样/超时/真实松开校验；主循环耗时按阶段分类日志。业务程序 PID、内核 boot_id 和退出日志用于区分重启类型。
- 背光源码及原配置保持 3.3.7 行为。实板验收和原故障证据单独记录，不把注入测试称为现场故障已解决。

# 3.3.7 — 2026-10-08

补齐在PWM亮度档正常退出的清理路径：先调用已验证的GPIO关闭路径恢复C7复用并输出低，再释放GPIO。3.3.6端点修复和已生成的正式包保留。本版加入使用实际平台驱动的板端连续档位与PWM档退出探针，验收在WSL构建、Wi-Fi上传和独立身份/空闲检查后执行。

# 3.3.6 — 2026-10-08

实板复现PWM11退出后GPIO1_C7复用仍为3，导致背光0%及退出无法关闭。背光重新获取GPIO55时，仅通过RV1106高16位写掩码恢复0xff538014的C7复用位并校验其余位保持；不修改按键、中键灯、相机、配对或保存亮度语义。3.3.5正式包及回退保留。

## 3.3.5 — 2026-10-08

- 背光硬件 PWM 占空比按实板反馈反向；0%/100%切回GPIO明确灭/亮，中间亮度以(100-percent)%输出；启用及退出PWM避免反向瞬时全亮。
- 中键：配对窗口呼吸、已配对且15秒内收到本机查询常亮、未连接/未配对慢闪、按键读取失败或Wi-Fi needs_attention快闪、正常退出熄灭。
- 预留低电双闪：两次125ms亮脉冲，周期2秒；优先级为故障、低电、配对、断线、正常。ADC尚未接入，运行时不触发低电，不伪造电量。
- 灯态仅显示状态，不改变中键配对、左右键拍摄或上传语义。
- ARM构建、主机按键/上传回归、PWM所有亮度端点、灯态/低电预留、UDP诊断与配对窗口测试通过。未部署实板，实体亮度方向、灯效与冷启动待人工验收。

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

## 3.3.10 — 2026-10-13

22 prompts use user-selected Microsoft Kangkang male voice (Yunxi service returned HTTP 403). speech_focus acquire/renew/release uses a 6-second lease and 600ms handover gap; capture completion queues its prompt before notifying the PC. Capture/upload leases remain independent.
