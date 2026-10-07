# 3.3.1 实施验收记录

目标：41eda5f2c5b00de1 / CAM-41eda5f2c5b00de1。

- ARM/host编译、灯态/信号/错误主机回归、现有upload/pending/discard/buttons回归通过。
- 板端五种灯态、背光、默认30秒、TERM清理、占用拒绝通过。
- 真实SSH入口调用、错序列号/未知密钥拒绝通过；现场WiFi未连接，本次SSH走ADB转发。
- 完整包校验、损坏拒绝、安装3.3.1→回退3.3.0→再安装3.3.1通过；配置与配对SHA256不变。
- 软件重启后state/version=3.3.1，instrument_app运行；三键=1/1/1，PWM2 disabled，GPIO32/55未导出。
- 未宣称实体亮灭/平滑、电流电压、物理按键和冷断电验收完成。用户确认接线正确，未提供电气测量数字。

Windows证据：调试/记录/light-test-20261007/{board-tests.json,python-light.jsonl,install-rollback-check.log,reboot-check.json}。

源码提交见release BUILD；正式tag instrument-v3.3.1的注释包含完整包SHA256。
