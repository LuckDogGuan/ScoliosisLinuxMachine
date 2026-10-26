# 仪器应用 3.3.34 — 故障诊断

两端故障定位与记录导出，单事件2KiB、单次512KiB、故障材料4MiB；进程标识绑定系统启动/PID/首次单调时间。支持断网留痕、空闲整理和历史材料补采。

配套上位机Windows3.3.40。本次仅构建和自动化，未安装实板；WSL网络集成测试失败与旧代码基线一致，不能计为通过。

详见源码 docs/fault-diagnostics.md 及Windows调试仓库 fault-diagnostics-20261026/README.md。
