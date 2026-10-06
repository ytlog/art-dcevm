# 待验证能力

## 活动 boot JIT 帧内联应用方法

目标手机：`DEVICE_001`，Android 13 / arm64。需要在一个尚未执行到应用方法内联点的活动 boot JIT 物理帧上发起结构替换，确认提交前该帧已转为解释器状态，或在旧定义未改动时安全拒绝。

当前 `ArrayList.forEach` → `BootInlineConsumer.accept` 专项在缩小回调后仍只有 JIT 缓存状态 17，`HGraph::InlineInto` 目标命中 0 次，未构造出所需帧。见 `evidence/source-engine/20261006T084146Z/probe.json`。预检中的保守扫描逻辑位于 `app/src/main/cpp/platform/android13/source_engine/entry.cc`；它尚无此目标形态的直接命中证据。

已试过设备 `dex2oat64 --compiler-filter=speed`：它能生成应用 AOT 帧。非调试 AOT sidecar 还能把同一 oat 内的应用静态调用折叠成常量，见 `evidence/device-aot/20261006T085612Z/oatdump.txt`；但应用 DEX 的 AOT 编译不会重新编译 boot `ArrayList.forEach`，不能替代上述 boot JIT 验证。按当前优先级先跳过此专项。以后若能稳定产生目标内联帧，再做提交前帧状态、拒绝/提交结果及恢复后返回值的双 Hook 顺序验证。

## 非调试 AOT 活动帧的即时切换

当前预检会拒绝无法确认可去优化的非调试 AOT 活动帧。两种 Hook 顺序已验证返回 66 时旧帧正常执行、帧退出后重试提交，见 `evidence/source-engine/20261006T091127Z/probe.json` 与 `evidence/source-engine/20261006T091149Z/probe.json`。若将来支持即时 DeOPT，需先验证该编译策略的栈图足以恢复任意暂停点的解释器状态，再将提交条件从安全拒绝改为实际切换成功；还需覆盖优化内联与多个并发帧。

当前 AOSP `runtime.cc::IsAsyncDeoptimizeable()` 明确只默认接受可调试 JIT；设备非调试 AOT sidecar 的调用者 `CodeInfo` 没有 Dex 寄存器映射（`evidence/device-aot/20261006T091108Z/oatdump.txt`），而可调试 AOT 对照产物有该映射（`evidence/device-aot/20261006T061031Z/oatdump.txt`）。因此不能仅放开现有 `oat->IsDebuggable()` 预检来宣称即时切换安全。
