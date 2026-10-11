# 真机 ART 运行库采集

从 arm64 真机提取 ART 相关 ELF，用于核对命名符号与固定源码声明。样本来自物理设备（本地连接或 Firebase Device Streaming），不使用系统镜像代替。采集成功不代表该版本已通过运行验证。

## 覆盖原则

目标 API 为 30、31、33、34、35、36、37（12L/API 32 不在验收范围）。每个 API 先采集一台 Pixel 作为基线，再补充其他厂商和芯片。ART Mainline 更新可能改变同一机型的库，因此按库 SHA-256 和 GNU Build ID 区分，并记录设备指纹、安全补丁日期和实际挂载的 APEX 版本。

## 采集命令

本地设备直接用 `adb`。使用 Firebase 时，用 Google [Android CLI](https://developer.android.com/tools/agents/android-cli) 的 [remote 命令](https://developer.android.com/tools/agents/android-cli/commands/device_remote) 预订物理设备，项目需启用 `devicestreaming.googleapis.com`；只使用已有项目和免费额度，额度不足时停止预订。

```sh
android --no-metrics device remote create <MODEL>/<API> --project=<PROJECT_ID>
python3 tools/collect-art-device.py --serial <SERIAL> \
  --origin firebase --project <PROJECT_ID> --reservation <RESERVATION_ID> \
  --expected-sdk <API> --compress --readelf <NDK_LLVM_READELF> \
  --output build/evidence/art-devices/<RUN>
android --no-metrics device remote remove <RESERVATION_ID> --project=<PROJECT_ID>
```

采集结束或失败都要立即释放预订。工具不安装应用、不 root、不修改系统。它拉取 ART/runtime APEX 与 system 中的 `libart`、`libopenjdkjvmti`、`libdexfile`、`libart-compiler`、`libartbase`、`libc++` 候选库，记录远端路径、SHA-256、Build ID 和命名符号清单；`--compress` 通过设备端 gzip 减少传输量。完整采集要求 ART、JVMTI、DEX、libc++ 四类核心库都存在且哈希核验通过。Android 15 起 `libart-compiler` 并入 `libart`，独立编译器库缺失属正常情况。

## 入库与核对

完整采集通过核验后，工具把库同步到 `assets/runtime_lib/`（目录规则见其中的 README）。随后运行：

```sh
python3 tools/check-structural-symbols.py
```

原始日志、清单与失败记录保存在已忽略的 `build/evidence/`；`assets/runtime_lib` 中的 SO 也不提交，不会打入 APK。
