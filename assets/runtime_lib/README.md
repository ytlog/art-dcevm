# 真机 ART 运行库参考资料

此处按用户要求保存从设备实际提取的完整 SO，供命名符号与固定 AOSP 声明核对。采集范围为 Android 11/API 30 及以上的 ARM64 真机；支持双 ABI 的设备可同时提取实际 ARM32 运行库。

目录规则：`api-<SDK>/<厂商>/<型号>-<产品代号>/<系统构建>-<整套库哈希前缀>/`。下面保留设备上的 `apex/…/lib64/`、`apex/…/lib/` 与 `system/lib64/`、`system/lib/` 路径，避免不同架构或 APEX 的同名库互相覆盖。同一系统指纹发生 ART Mainline 更新时，库集合哈希不同，建立独立目录。

每个构建的 `manifest.json` 记录来源（Firebase 或 local）、设备指纹、API、厂商、型号、页大小、采集时间、远端路径、SHA-256 和 GNU Build ID；根目录 `index.json` 汇总已存储的设备。只有完整采集且通过哈希核验的库集合才能进入本目录。原始日志、符号清单与失败记录仍保存在 `build/evidence/`。

`tools/collect-art-device.py --include-32-bit` 在默认 ARM64 采集之外增加 ARM32 路径；设备必须实际声明支持 `armeabi-v7a`。每个文件独立核对 ELF 位数、机器类型和目录，并记录 `libraries[].abi`。每种请求的架构都必须完整采集必需库；缺少 ARM32 库不能由同名 ARM64 库充数。该选项只读取真机文件，不安装或启动应用。

`device.mountedApexVersions` 记录实际挂载的 APEX 版本。ART Mainline 更新与系统 API 不完全绑定；同一 API、不同 ART 版本的资料仍按库集合哈希区分，不能直接复用私有声明。历史样本的 APEX 信息可由保留的原始 mount 日志复核补充，保留日志路径与哈希，不修改旧采集报告。

后续设备先实际读取每个 SO 的 SHA-256。与已有真机参考文件完全相同且本地哈希复核通过时，复用相同字节，减少云端传输；不同内容仍从当前真机提取。原始 evidence manifest 的 `deviceSha256`、`reusedAsset` 记录该过程，系统版本或型号相同不足以触发复用。

```sh
python3 tools/store-art-runtime-libs.py <完整采集的 manifest.json> [...]
```

这里是工程根目录的参考资料，不是 Android 模块的 `src/main/assets`，不会自动打入 APK。根据项目规则，SO 和设备清单由 `.gitignore` 排除；仅提交本说明与采集/整理工具。符号存在不证明私有 ABI 或完整结构演进已经通过运行验证。
