# 真机 ART 运行库参考资料

此处按用户要求保存从设备实际提取的完整 SO，供命名符号与固定 AOSP 声明核对。采集范围为 Android 11/API 30 及以上的 ARM64 真机。

目录规则：`api-<SDK>/<厂商>/<型号>-<产品代号>/<系统构建>-<整套库哈希前缀>/`。下面保留设备上的 `apex/…/lib64/` 与 `system/lib64/` 路径，避免不同 APEX 的同名库互相覆盖。同一系统指纹发生 ART Mainline 更新时，库集合哈希不同，建立独立目录。

每个构建的 `manifest.json` 记录来源（Firebase 或 local）、设备指纹、API、厂商、型号、页大小、采集时间、远端路径、SHA-256 和 GNU Build ID；根目录 `index.json` 汇总已存储的设备。只有完整采集且通过哈希核验的库集合才能进入本目录。原始日志、符号清单与失败记录仍保存在 `build/evidence/`。

```sh
python3 tools/store-art-runtime-libs.py <完整采集的 manifest.json> [...]
```

这里是工程根目录的参考资料，不是 Android 模块的 `src/main/assets`，不会自动打入 APK。根据项目规则，SO 和设备清单由 `.gitignore` 排除；仅提交本说明与采集/整理工具。符号存在不证明私有 ABI 或完整结构演进已经通过运行验证。
