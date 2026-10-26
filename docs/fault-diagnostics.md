# 被动故障诊断 evidence_v1

正式运行由Flutter与仪器直连，不依赖Python。应用新增目录 `/opt/instrument/logs/`，应用逐文件升级保留此目录，整机刷写不保证保留。

## 边界

记录运行事件、按键/许可/拍摄/文件/上传的已有事件，补记UDP拒绝、重复事务和失败原因。单行最多2048字节，过长保留有效JSON并标记截断。抢锁失败不等待，下一条记录显示本进程丢失计数。重复事件采样合并，计数随下一条成功记录输出；突然断电前未写出的尾部仍可能缺失。

故障标记立即写入；空闲后收集前60秒、后15秒的可用记录，带当前快照和时间依据。时钟修改、晚收集、轮转均可能造成缺口，partial/window_incomplete 不得当作完整证明。故障快照含实际采集时间，不等于硬件内存转储。

## 协议

UDP37020沿用V3 device_id/host_id/boot_id/sequence/nonce与配对源IP验证。
- evidence_collect: fault_id、token、target_capture_id、target_boot_id、utc_ms；幂等事务，返回接受状态。
- evidence_status: 查询整理状态，不消耗业务控制序号。
- evidence_detail: duration_ms 0..600000，0关闭；到期自动关闭。
- hello_reply增加evidence_v1能力及最新故障标记。

材料由仪器POST到已配对电脑 `/api/v3/diagnostics/evidence`，携带X-Evidence-Token、X-Device-Id。电脑核验临时票据、设备、源IP、故障编号和512KiB上限。只向已配对目标传送，不允许请求指定任意上传地址。

许可、录制、收尾或上传忙时保留待处理请求；采集不启相机、不操作灯、不建立硬件诊断租约。采集工作进程25秒有界，拍摄开始优先取消它，电脑可稍后重试。

## 配额与验收

runtime三份各约3MiB，startup三份各约1MiB；故障8份每份最多512KiB。故障过期在整理时清理，空闲后可补采。业务pending视频不属于诊断清理对象。

运行 `python3 tests/test_fault_diagnostics.py` 验证无真实UDP的协议与存储边界；`make -B all mux-adapter` 做ARM构建。网络集成测试需可用WSL回环网络；完整结果与旧代码基线对比见Windows诊断验收记录。实板验收另按手册执行。
