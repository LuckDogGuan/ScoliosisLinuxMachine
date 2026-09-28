# Pico Zero instrument_app

下位机业务程序，C/uClibc。仪器不存学生名单、姓名或 Wi-Fi 密码。电脑端对照 `D:\code\Scoliosis\ScoliosisFlutter`。产品范围以 2026-09-28 已确认的单设备 V1 为准：一台电脑、一台当前仪器；多电脑抢连和首次凭据交换本版不做。

## 源码

| 目录 | 作用 |
| --- | --- |
| `src/app` | 主循环：报到、取命令、录像、补传 |
| `src/net` | UDP 发现、HTTP、Wi-Fi 地址和信号 |
| `src/capture` | 通过 rkipc 原生 socket 录 10 秒 |
| `src/storage` | 目录、待传文件、命令状态 |
| `src/audio` | 音量和提示音 |
| `src/board` | 电池、按键、灯。目前都是空桩 |
| `src/config` | 读取 `config.json` |
| `include/app.h` | 版本号、板上路径、模块接口 |

版本号 `INSTRUMENT_VERSION` 为 `0.0.1`。

## 构建与检查

在 WSL `Luckfox-Ubuntu-22.04` 的本目录运行 `make`。默认使用 `/home/guanjx/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-gcc`，`-Wall -Wextra -Werror -O2`。`make check-host` 用宿主 gcc 做配置检查，并用 Python 检查含 NUL 字节的视频上传。`third_party/cjson` 编进程序，不依赖板端 cJSON 动态库。

`make check-host` 不覆盖 UDP 发现、命令状态机和 rkipc 录像。这三项不能用编译通过代替实板结果。

## 运行前

1. 从 `config.example.json` 复制配置。仪器号、热点名称和电脑地址按现场填写。Wi-Fi 密码只在板端 `wpa_supplicant.conf`。音量保持 **10**。样例一度写成 15，已改回 10，与已确认的产品决定和板上读回一致。
2. 程序优先播放 `/userdata/instrument/sounds/<名字>.wav`，没有再找 `/userdata/sounds/`。启动时会把旧声音目录和旧 pending 目录里的 wav、mp4、json 迁到下面的新目录；目标已存在的文件不覆盖。
3. `/userdata` 剩余不足 16 MB 时拒绝开录，并播 `storage_low`。不打开 rkipc 循环删除。这条边界还没有在实板上故意造出来过。
4. 开机脚本 `调试/配置/方案/S80instrument` 启动的是 `/userdata/instrument/bin/instrument_app`，配置是 `/userdata/instrument/config.json`。先跑 `instrument_app --check-config /userdata/instrument/config.json`。2026-09-28 上午那次重启验证用的是旧路径 `/userdata/instrument/instrument_app`，哈希 `f8a12ea86bd3f3ba33b0be9eebdaf27c137650c54c1b27689e9822ba0dadb620`。那个哈希不是现在这棵源码编出来的。新二进制要放到脚本指向的 `bin/` 下，否则开机找不到程序。

板上目录：

| 路径 | 用途 |
| --- | --- |
| `/userdata/instrument/bin/instrument_app` | 当前开机脚本要执行的程序 |
| `/userdata/instrument/config.json` | 仪器号、电脑地址、音量、录像参数 |
| `/userdata/instrument/state/command.json` | 当前命令阶段：`none` / `seen` / `acked` / `started` |
| `/userdata/instrument/state/version` | 启动时写下的版本号 |
| `/userdata/instrument/capture/writing/` | 从 rkipc 目录挪过来的临时片 |
| `/userdata/instrument/capture/pending/` | 待上传或待确认的 mp4 和同名 json |
| `/userdata/video0/` | rkipc 自己写下的按秒文件名。程序不把这里的残片当可上传文件 |

## 一次拍摄

空闲时约每 3 秒 `POST /api/v1/hello`。hello 带设备号、版本、IP、热点、信号、剩余空间、摄像头名和电池 `present`/`absent`。电脑返回 `paired` 后，才取拍摄命令。

有命令号时走这条状态，并写进 `command.json`：

1. `GET /api/v1/pending-command` 得到 `capture` 和 `command_id`，记为 `seen`。
2. `POST /api/v1/command/ack`。成功后记为 `acked`。
3. 生成 `capture_id`，`POST /api/v1/capture/start`。电脑回 200 且 `ok` 才开录，并记为 `started`。409 播 `capture_failed` 并清掉这条命令。网络错误先查 `GET /api/v1/command-receipt`；只有回执已经是 started 才继续录，否则本轮停下，避免录错学生。
4. 录像约 10 秒。期间主循环停住，不发 hello。
5. 上传。只有 HTTP 200 且 JSON `ok` 为真，或随后 `GET /api/v1/captures/<capture_id>/receipt` 的 `stored` 为真，才删板上的 mp4 和 json。否则留在 pending，并播 `saved_on_device`。

没有 `command_id` 的旧回复仍会直接 `capture/start` 再录像。这是兼容旧电脑端，不是新的主路径。

每次 hello 成功后，补传 pending 里的一个 mp4。失败的文件会在后续 hello 里继续试，不是开机只试一次。pending 还在时，不接收新的拍摄命令。`started` 但 writing 和 pending 都空了，会清掉命令状态。writing 里留下任何文件时，不自动清状态，新的拍摄会被挡住，需要人工看这个目录。

UDP `37020` 收到合法 `discover` 后，单播 `here`（版本 2、原样 nonce、设备号、固件 `0.0.1`），并把内存里的电脑地址改成广播来源的 IP 和端口。这个地址不写回 `config.json`，重启后仍用配置文件里的 `pc_base_url`。

录像前读 rkipc 的帧率、码率和 GOP，设成 `60/2`、4000 kb/s、GOP 30，再 `rk_storage_record_start`，等到 `duration_ms`，然后停止并恢复原参数。rkipc 先把片写到 `/userdata/video0/`。只有本次多出唯一一个、头部含 `ftyp`、体积至少 100000 字节的 mp4，才先改名为 `writing/<capture_id>.tmp`，再改成 `pending/<capture_id>.mp4`，并写不含姓名的 json。无法判定时原文件留在 `video0`。视频正文用 TCP 发送，带 `Content-Length`。BusyBox `wget --post-file` 对首字节为 NUL 的文件会发出空正文，所以只用于短 JSON，不用于视频。

## 未完成功能

下面是 2026-09-28 对照当前源码和已有实板记录后的缺口。代码在仓库里，不表示实板已经通过。

| 事项 | 现在怎样 | 为什么还没完成 | 怎样才算完成 |
| --- | --- | --- | --- |
| 局域网发现 | 程序会听 `37020` 并回 `here`，也能改内存里的电脑地址 | 同日实板测试里，电脑发出的广播和单播都没有进入仪器 UDP。仪器对电脑的 ARP 一直是 `INCOMPLETE`。这是无线客户端隔离，不是报文格式错误 | 电脑发出 `discover` 后收到含 `CAM-000123` 和 `0.0.1` 的 `here`；数秒内 hello 到达新的电脑地址。关 AP 隔离或电脑改接网线之前，不要再改发现报文 |
| 发现不能当配对 | 任何能把合法 `discover` 送到仪器的主机，都会把后续 hello 和上传指到自己 | 本版明确不做多电脑凭据。发现只表示“被看见” | 保持现状并在现场只用一台电脑。不要把收到广播写成已经允许上传 |
| 命令状态机 | `seen` / `acked` / `started` 和回执查询已经写好，电脑端有对应接口 | 实板完整链路测的是更早的“取到 capture 就开录”。新的确认、回执和掉电恢复没有在板上走通 | 一台实板上完成：取命令、确认、开录、上传、回执丢失后仍能查到已保存、失败时 pending 还在 |
| 上传补传 | 确认保存后才删片；hello 成功就再试一个 pending | 断网留片只在宿主测试里看过。测试热点还有过超时。没有文件 SHA-256，重复上传只认 `capture_id` | 稳定网络上连续多次拍摄，记录每次文件大小和两边哈希；人为断开一次，确认片留在 pending，恢复后补传成功才删除 |
| 录制时推流 | 录像调用不会关 RTSP | 实板录制中 RTSP 仍能 PLAY。现有 rkipc socket 没有已确认的单独暂停接口。不重启编码器 | 找到可单独暂停 RTSP 的接口并实板证明录像 10 秒内拉不到流；在此之前，拍摄前手动关掉预览 |
| 断电残片 | 停录成功后才从 `video0` 挪到 `writing` 再进 pending | rkipc 自己先写 `.mp4`。中途断电的文件留在 `video0`，程序不会上传它。`writing` 里一旦有文件，命令状态不会自动清除 | 规定残片是保留待查还是可删除。确认程序不会把未收尾文件上传 |
| 帧数 | 目标是 `60/2`、10 秒、GOP 30 | 已测到的片是 10.000 秒、293 帧、约 3.426 Mb/s，不是 300 帧 | 在稳定网络上再录几段，确认这是编码器节奏而不是单次异常 |
| 提示音 | 只播放 `capture_done`、`uploaded`、`saved_on_device`、`storage_low`、`capture_failed`。同一句 30 秒内不重复 | `please_connect`、`wifi_connected`、`wifi_disconnected` 没有接到播放逻辑。配对成功或失败的播报也没有。录制 10 秒时主循环不播这些句子，但没有单独把喇叭静音 | 五句在对应状态下实板听到；决定另外三句和配对播报还要不要做。音量继续用 10 |
| 按键、灯、电池 | `button_capture_pressed` 固定返回 0，`light_update` 不操作，`battery_present` 固定为未安装。配置里的 `battery.present` 必须是 false | 外接按键、灯和主电池未到。不能动 BOOT、RESET、板载 `work` 灯和 GPIO123 | 硬件和电路核对后再接 GPIO。在此之前本地按键录像不会发生 |
| 低空间拒绝开录 | 剩余不足 16 MB 会拒绝并播 `storage_low` | 实板还没有把剩余空间压到这条线 | 制造不足 16 MB 的条件，确认不开录、不删未传片 |
| 开机路径 | 脚本指向 `bin/instrument_app` | 已验证重启的那次二进制在上一级目录，而且是改目录结构之前的程序 | 用当前源码重新交叉编译，放到 `bin/`，重启后进程、版本文件和 Flutter 卡片一致 |
| 长期稳定性 | 单次完整上传和单次重启做过 | 没有多次断电、掉线重连和长时间运行记录 | 同一路由器上连续多次 10 秒拍摄，保留文件大小、哈希和 syslog |

## 已有实板结果

- 2026-09-28，改目录之前的程序做过一次完整链路：`CAM-000123` 录制并上传 `CAM-000123-b48e5c6f`，电脑保存 4,285,097 字节并归到测试学生 T001，板端确认后 pending 清空。`ffprobe` 为 HEVC、1280×1080、10 秒、293 帧。随后测试热点出现超时，不能把这一次当成稳定性结论。
- 同日交叉编译和 `make check-host` 曾通过。源码拆到 `src/` 之后，提交前需要重新跑这两项；通过仍然不等于实板录像通过。
- 板上 `DAC LINEOUT Volume` 读回过 10。
- 发现程序曾在板上监听到 `udp 0.0.0.0:37020`，但没有收到发现包。记录见 Windows 工程 `调试/记录/UDP广播发现_2026-09-28.md`。
