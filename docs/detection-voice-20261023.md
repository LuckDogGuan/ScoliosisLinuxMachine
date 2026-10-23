# 检测语音调整验证（2026-10-23）

- 权威源码：WSL Luckfox-Ubuntu-22.04:/home/guanjx/luckfox/Scoliosis。
- 修改基线：17d1963939a5（3.3.30）；本次为未发布改动，未更新实板。
- capture_start.wav 改为“开始检测”；Microsoft Kangkang、rate=-1、16000Hz、单声道、16bit PCM，1.965秒。
- WAV SHA256：5cf80d9a27489f631410653ef76e2f37f0218515a0e53bc3f7167912ad476dba。
- scripts/generate_detection_prompt.ps1 可在Windows中重建音频；生成后通过WSL导入sounds/capture_start.wav，并更新清单。
- V3 upload_messages 和旧版 reap_upload 移除上传成功播报；失败仍提示saved_on_device，回执与文件处理不变。
- GitNexus：两个函数均为LOW风险，分别由runtime_run、legacy_run直接调用。分析前已核对索引副本与权威源码对应文件SHA256一致。
- ARM及host编译通过（-Wall -Wextra -Werror）；test_capture_rhythm.py、test_startup_network_prompts.py通过；22个WAV的清单哈希核对通过；源码检查确认两处上传成功播报均移除且失败提示保留；git diff --check通过。
- test_pending_upload.py在首次本机HTTP回执查询失败；WSL有127.0.0.1监听，但直接socket连接超时。本项不记为通过，未为此改网络或上传实现。
- 未发布包、未刷机，实板听感未验收。
- 回滚：恢复本次src/app/runtime_v3.c、src/middleware/legacy.c、sounds/capture_start.wav和sounds/stability-voice-manifest.json的基线版本；勿回滚后续其他工作。

## 后续网络诊断（2026-10-23）

原test_pending_upload.py在临时独立网络命名空间中完整通过（unshare -n，仅开启该命名空间lo，退出即销毁）；确认首次失败与WSL默认镜像网络的IPv4主机转发异常有关。默认网络未修复；两个发行版的127.0.0.1连接均超时，Windows对应连接正常。未修改TUN＋规则模式或任何现有网络配置。完整证据见Windows调试/记录/wsl-network-20261023/README.md。

## 3.3.31 封装与设备更新（2026-10-23）

- 在原有改动基础上将版本提升为3.3.31，旧包3.3.30保持不变；源码提交后由现有package_image.sh重新构建并封装。
- ARM/host重新编译、拍摄节奏、启动网络提示、独立网络命名空间内上传回执与配对提示回归均通过；WSL默认mirrored IPv4故障与固件逻辑分开验收。
- 安装采用Windows现有Wi-Fi/SSH烧录CLI，安装前核对实时设备身份、主机密钥与空闲门禁；保留配置、配对和待传数据，导出回退审计。
- 原语音清单为CRLF，保留原格式；git -c core.whitespace=cr-at-eol diff --check通过。
- 实际安装、重启、逐文件哈希及播放检查结果以Windows调试/记录/detection-voice-flash-20261023/为准；封装前不宣称实板通过。
