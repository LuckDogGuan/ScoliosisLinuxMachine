# 正式版本（唯一归档）

源码、构建、Git 和正式发布包只保存在 WSL：`/home/guanjx/luckfox/Scoliosis`。
每个 `releases/<版本>/` 只保留 `instrument-v<版本>.tar.gz`、`BUILD.txt`、`VERSION.md`、`SHA256SUMS`。
从提交后的源码运行 `scripts/package_image.sh`，它会编译、校验并调用 `scripts/package_bundle.sh` 封包；校验压缩包与 staging 内容一致后，逐文件删除临时 payload。
版本目录已存在时禁止重新发布；单独调用 package_bundle 对已归档版本只校验，不重打。

末位版本更新，其他位须用户通知。3.3.1 起统一使用 `instrument-v<版本>`。历史标签保持原名及指向。
BUILD 中 `source_commit` 指实际源码提交，旧 `source_base_commit` 仅指源码基点，不宣称包含当时未提交变化；tag 可指包含归档的后续提交。
本次原先只有 payload 的 3.1.0/3.2.0/3.2.1/3.2.2 已原样封存，不宣称重新编译。缺少的 3.2.1/3.2.2 历史标签在本次归档提交补建，并注明来源。
完整映射与逐文件清理校验见 `../docs/release-organization-20261007.json`。

`baseline-20261001` 保存改造前唯一回退包；其中源码与实板旧二进制来源不同，未证明可逐字节重编译。
UVC 构建材料在 `../tools/uvc-v1.0.0/`，Windows 仪器测试工具保留其必要 runtime payload；二者职责不同。

历史包可按需解包到临时目录，包内 `SHA256SUMS` 校验原始内容。历史文档中的 image/payload 路径属于当时记录，当前入口以本说明为准。
