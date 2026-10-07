# Firebase 真机 ART 采集

采集用于 Android 11–17 私有能力移植的实际 ELF、命名符号与版本证据。SO 来自 Firebase Device Streaming 的物理设备，通过 ADB 拉取；不以系统镜像代替真机。采集成功与符号存在均不代表完整结构演进已经通过运行验证。

## 覆盖选择

先在每个 API 30–37（包括 Android 12L/API 32）采集一台 Pixel，建立跨版本基线。然后补充三星各可用 API、不同芯片和旗舰/中端机型，以及小米、OPPO、一加、realme、Motorola、Sony、Nothing 和传音设备。对同一 API 的不同厂商分别核对；ART Mainline 更新也可能改变相同机型的库，按库 SHA-256 与 GNU Build ID 聚合，保留设备指纹与安全补丁日期。

2026-10-08 登录后读取的官方串流目录提供以下候选；这是目录结果，尚不是已提取或已校准清单：

| 厂商 | Android 11 及以上可选 API | 示例 |
| --- | --- | --- |
| Google | 30–37 | Pixel 5/30、6/31、6a/32、7/33、8/34、8 Pro/35、10/36、11/37 |
| Samsung | 31、33–36 | A21/31、S22 Ultra/33、S24/34、S23/35、S26/36；另有平板、折叠屏 |
| Xiaomi | 35 | Xiaomi 14 |
| OPPO | 34 | A38、A79 5G |
| OnePlus | 34 | 10T 5G、Nord CE 2 Lite 5G |
| realme | 30、35 | RMX3231、C53 |
| Motorola | 30、33–36 | moto g(9) play、g 5G (2022)、edge 40、g96、g85 |
| Sony | 34 | Xperia 10 VI |
| Nothing | 36 | Phone (4a) |
| TECNO / INFINIX | 31、33、35 | SPARK 10C、SMART 8、POVA 7、NOTE 50 Pro+ |

vivo、荣耀、华为没有出现在本次串流目录中。三星的 30/32、小米的其他版本等也仍是缺口，不能将其他厂商或本地设备计作这些 Firebase 样本。目录随时间变化；每批采集前保存最新输出。

## 操作

Google 官方 [Android CLI](https://developer.android.com/tools/agents/android-cli) 的 [remote 命令](https://developer.android.com/tools/agents/android-cli/commands/device_remote) 可以预订并连接物理设备。首次登录需完成账户验证与授权，项目需启用 `devicestreaming.googleapis.com`。使用已有项目和免费额度，不自动开通付费结算。免费额度不足时停止新的预订。

```sh
android --no-metrics auth login
android --no-metrics device remote projects --all
android --no-metrics device remote models --project=mobby-appfunctions-test
android --no-metrics device remote create redfin/30 --project=mobby-appfunctions-test
adb devices -l
python3 tools/collect-art-device.py --serial <REMOTE_SERIAL> \
  --origin firebase --project mobby-appfunctions-test --reservation <RESERVATION_ID> \
  --expected-sdk 30 --compress --readelf <NDK_LLVM_READELF> \
  --output build/evidence/art-devices/<UNIQUE_DEVICE_RUN>
android --no-metrics device remote remove <RESERVATION_ID> --project=mobby-appfunctions-test
```

采集结束或失败都应立即释放自己创建的预订。预订、串流和 Test Lab 的目录与额度可能不同；不要把 Test Lab 测试设备列表当作可连接的串流清单。采集工具不安装应用，不 root，不修改系统；记录不可读取或缺失的库。

工具拉取 ART/runtime APEX 与 system 中的 ARM64 release/debug 候选库，包括 `libart`、`libopenjdkjvmti`、`libdexfile`、`libart-compiler`、`libartbase`、`libc++`。每个文件记录完整远端路径、哈希、Build ID 和命名符号清单。`--compress` 通过设备 gzip 和 `adb exec-out` 减少传输量，本地解压后保存完整 ELF；`--pull-timeout` 默认每库 300 秒，适用于较慢的串流连接。路径存在不证明应用实际加载了该路径，最终测试时还需核对进程实际使用的库。

```sh
python3 tools/audit-art-device-matrix.py build/evidence/art-devices/*/manifest.json \
  --output build/evidence/art-devices/matrix.json
```

矩阵审计先核验 ELF 与符号清单哈希，再统计当前 Android 13 后端的 87 个 C++ 符号字面量是否存在。包含可选能力，缺失不直接等于运行失败；跨库存在也不保证调用方指定库能解析该符号。后续逐个核对实际解析库与固定 AOSP 声明，不通过反汇编推导函数地址或字段偏移。在线 AOSP 固定源码审计继续使用 `tools/audit-art-source.py`。任何新增 Profile 都需真实运行行为回归后才能开放。

原始采集的 SO、CLI、日志、矩阵均保存在已忽略的 `build/evidence/`。完整采集通过核验后，工具自动将 SO 同步到工程根目录的 `assets/runtime_lib/`，按 API/厂商/型号/系统构建与库集合哈希分类，详见其中的 README。两个位置的设备 SO 均不提交；`assets/runtime_lib` 是参考资料，不自动打入 APK。
