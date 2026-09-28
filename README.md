# Pico Zero instrument_app

下位机业务程序，C/uClibc。电脑端协议见 `D:\code\Scoliosis\Luckfox\调试\软件架构\Pico_Zero_拍摄仪器软件框架设计.md`。仪器不存学生名单、姓名或 Wi-Fi 密码。

## 构建与检查

在 WSL `Luckfox-Ubuntu-22.04` 的本目录运行 `make`。默认使用 `/home/guanjx/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-gcc`，`-Wall -Wextra -Werror -O2`。`make check-host` 构建宿主版本，检查配置和含 NUL 字节的视频上传。`third_party/cjson` 是本目录现有的 MIT 许可源码，用于配置和短 JSON 响应；目标程序编入该源码，不依赖板端 cJSON 动态库。测试脚本仅在 WSL 宿主运行，板端业务不需要 Python。

## 运行前

1. 从 `config.example.json` 复制配置，填入实际仪器号、同一热点名称和电脑局域网 IPv4:端口。Wi-Fi 密码只在板端现有 `wpa_supplicant.conf`。默认运行路径为 `/userdata/instrument/config.json`；也可传配置路径作唯一参数。
2. 将 `sounds/*.wav` 放到板上 `/userdata/sounds/`。程序用 `amixer` 设音量，用 `aplay` 播放。当前音量暂设 **10**（`config.example.json` 中的 `audio.volume`），以后在配置中调整。按键和灯尚无产品硬件，不读 GPIO、不控制板载 `work` 灯。
3. 确认 `/userdata` 仍有至少 16 MB 空间、`rkipc` 正常运行、电脑 Flutter 服务监听 8080。程序创建 `/userdata/capture/pending/`，不打开 rkipc 的自动循环录制。
4. 先运行 `instrument_app --check-config config.json`。联调可把二进制和配置放 `/tmp`，手动运行 `instrument_app /tmp/config.json`；当前不设开机自启。

## 一次拍摄

空闲时每 3 秒 `POST hello`，每秒 `GET pending-command`。电脑下发的 `capture` 只有 `capture/start` 回 200、`{"ok":true}` 才录像；409 或网络错误直接取消这次，避免视频锁错学生。未来本地中键触发可离线录像并保留文件，但当前按键桩固定不触发。录像前读取并保存 rkipc 的帧率、码率和 GOP，通过 `/var/tmp/rkipc` 的原生 socket 把目标设为 `60/2`、4000 kb/s、GOP30，`rk_storage_record_start`，等待 10 秒，`rk_storage_record_stop`，再逐项恢复原值。视频由 rkipc 写入 `/userdata/video0/`；仅当本次出现唯一新 MP4 且头部有效时，移动到 `pending/<capture_id>.mp4`，另写不含姓名的同名 JSON。无法判定的新文件保留原处并记 syslog。

MP4 用标准 TCP socket 流式 POST，显式 `Content-Length`；仅在 HTTP 200 和 `{"ok":true}` 同时成立时逐个删除本地 MP4、JSON。失败保留 pending；下一次启动、成功连到电脑后，每个旧 MP4 补传一次。板端 BusyBox `wget --post-file` 对首字节 NUL 的二进制文件实际发送 0 字节，故它只用于小 JSON 请求，不能上传视频。

## 验证记录与当前限制

- 2026-09-28，`make -B` 用板端交叉编译器和 `-Wall -Wextra -Werror` 构建通过；`make -B check-host` 通过配置检查、含 NUL 字节 MP4 的上传、拒收和错误配置检查。
- 同日完成过一次仪器到 Flutter 的完整链路：仪器 `CAM-000123` 收到电脑命令，录制并上传 `CAM-000123-b48e5c6f`，电脑把 4,285,097 字节文件归给测试学生 T001，板端收到确认后清空 pending；`ffprobe` 显示 HEVC、1280×1080、10 秒、293 帧。后续再试时测试热点出现连接超时，因此完整链路的持续可靠性仍未确认。
- 板上实测 `DAC LINEOUT Volume` 当前为 10。配置样例也为 10；正式部署时需确认实际配置路径中的值仍为 10。
- 录像期间 rkipc 的 RTSP **仍可被外部客户端拉流**。实板证明 storage 录像并不会关闭 RTSP；开始拍摄前应关闭 VLC/其他预览客户端。尚无经验证的独立 RTSP 暂停接口，程序不重启编码器。
- 本程序尚未作为整套系统做持续运行、断电恢复或多仪器压力测试。`60/2` 是传给 rkipc 的目标帧率；实测文件在 10 秒内有 293 帧，仍需核查真实采集/编码节奏。
- rkipc 原生产物先落在 `/userdata/video0/` 的按秒文件名，停录后才移动到 pending；录像中断会留下原目录文件，需要人工核对。程序检查当前及随后两秒的撞名，不覆盖既有 pending 文件。
- 没有产品按键、灯、电池与专用路由器。按键/灯为明确的空桩，电池报 absent。自动重连、失败视频后台重试、开机自启留待后续。
