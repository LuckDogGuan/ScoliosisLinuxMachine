# 当前 Luckfox 板部署文件

S80instrument 安装位置为 `/oem/usr/etc/init.d/S80instrument`；RkLunch.sh 为本板当前 `/RkLunch.sh`，仅对 RKiPC 注入 `luckfox_mux_adapter.so`，保留原厂其他启动逻辑。移植另一块 Linux 板时重新核对平台和启动配置。

版本包程序和库放 `/opt/instrument/bin/`，声音放 `/opt/instrument/sounds/`。保留现场 `/opt/instrument/config.json`、配对身份、Wi-Fi、capture/pending、state/events 和上位机学生关联。示例配置仅用于首次初始化，不覆盖已有配置。换程序前关闭电脑拍摄许可，等待当前录像和文件收尾，再备份程序及配置。恢复程序后通过重启或 init 脚本正常启动，读取真实 GPIO。

回滚使用 `image/baseline-20261001/instrument_app` 与原 RkLunch.sh，并参考板端 `/opt/instrument/backups/before-ui-pair-layer-20261001-1522.tar` 的单独配置。不要整目录恢复覆盖新视频及新增归属数据。
