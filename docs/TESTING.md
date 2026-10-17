# 测试与诊断指南

[English](TESTING.en.md) | 中文

本页说明如何构建、执行测试与核对诊断；已有公开结论见 [COMPATIBILITY.md](COMPATIBILITY.md)，拒绝规则见 [LIMITATIONS.md](LIMITATIONS.md)。测试结束后的诊断用于证明覆盖，不应被接入方用作提交前开关。

## 准备与主机检查

源码构建需要 JDK 17 或以上、Android SDK 37、NDK 27.2.12479018、CMake 4.3.4，并在本地 `local.properties` 配置 `sdk.dir`。示例应用、库 Debug 与库 Release 必须分别构建，才能核对测试隔离：

```sh
mkdir -p build/evidence/documentation-check
./gradlew :app:assembleDebug :artdcevm:assembleDebug :artdcevm:assembleRelease
python3 tools/check-interface-abort-isolation.py
python3 tools/check-doc-links.py
for test in tools/test-*.py; do
  python3 "$test" > "build/evidence/documentation-check/$(basename "$test" .py).log" 2>&1 || exit 1
done
```

主机替身不执行 ART 对象、真实 Hook 或收集器，不能替代设备 ABI 验证。

`python3 tools/test-runtime-thread-host.py` 编译生产线程服务与事务上下文，以命名服务替身覆盖七版 Thread、JNI/tid/标记位拒绝、缺失 Mark、Native 探测后标记位变化、嵌套上下文恢复及 CC/CMC 调用边界；另覆盖 `VerifiedThread` 的编译期约束、`LayoutFor`、相同 JNI 前缀的正反断言，以及映射不可用原因不被线程探测覆盖；该主机测试不模拟收集器算法。

`python3 tools/test-structural-install-host.py` 编译公共生产模块，以公开 JNI/JVMTI 表与 ShadowHook 替身验证安装顺序、幂等性、inline hook 失败时不尝试 JNI 覆盖、JNI 表失败后保留基础能力，以及映射名称缺失后线程服务仍准备且诊断保留。测试不执行 ART 对象、收集器或真实 Hook，不能代替设备 ABI 验证。

## 设备回归

`DEVICE_SELECTOR` 是待替换的设备选择器占位符；`--device-id` 使用匿名标签。脚本会安装指定 APK、结束旧示例进程、删除旧报告并从初始定义开始，核对已安装 APK 的 SHA-256 与完整场景顺序。它操作示例包 `dev.artdcevm.lab`。

```sh
python3 tools/run-device-tests.py --serial DEVICE_SELECTOR --device-id android-device-1 \
  --apk app/build/outputs/apk/debug/app-debug.apk \
  --scenario-group full --checkjni-mode normal
python3 tools/run-device-tests.py --serial DEVICE_SELECTOR --device-id android-device-1 \
  --apk app/build/outputs/apk/debug/app-debug.apk \
  --scenario-group full --checkjni-mode checkjni
```

`ordinary` 保留普通类新增接口，只跳过接口目标；用于接口尚未启用的版本。`interface-abstract` 便于定位新增 abstract 的问题，不能替代完整回归。`--timeout` 调整仪器超时，`--evidence-root` 必须位于忽略的 `build/evidence/` 下。报告须覆盖全部预期场景、错误码、旧定义与后续操作；不能只看仪器进程的退出码。

## 失败恢复与故障隔离

完整仪器在同一进程检查失败后的旧行为、后续父类/接口事务和 GC：

```sh
adb -s DEVICE_SELECTOR shell am instrument -w -r -e checkjni false \
  dev.artdcevm.lab/com.github.ytlog.artdcevm.testing.InterfaceAbortInstrumentation \
  > build/evidence/documentation-check/abort-normal.log 2>&1
adb -s DEVICE_SELECTOR shell am instrument -w -r -e checkjni true \
  dev.artdcevm.lab/com.github.ytlog.artdcevm.testing.InterfaceAbortInstrumentation \
  > build/evidence/documentation-check/abort-checkjni.log 2>&1
```

只测普通类时加入 `-e scenario ordinary`。`-e runtimeProbe 13`、`14`、`15` 分别在独立进程覆盖 Thread、HandleScope、holder 探测拒绝。故障编号以 [fault_injection.cc](../app/src/debug/cpp/testing/fault_injection.cc) 与 [InterfaceAbortInstrumentation.java](../app/src/debug/java/com/github/ytlog/artdcevm/testing/InterfaceAbortInstrumentation.java) 为准；删除遍历回调后，6、7、9、10 也已删除。

当前恢复检查还覆盖准入、解析重定向和数组转存的 Hook 边界异常、Native 作用域构造失败与嵌套上下文恢复。pending 根必须覆盖尚未入私有数组的替换类。分配故障注入不能证明真实内存耗尽可恢复。

`InterfaceAbortInstrumentation` 在同一进程中验证真实 DEX 校验失败（错误的返回指令，以及以整数为接收者调用 `String.length()`，均返回 62）、链接产生类之后返回失败、完整分配后失败、实例分配失败和实际映射探测拒绝，再重定义原父类、成功更新接口并 GC。类型不兼容的调用失败后先显式 GC，再执行后续事务。ClassFileLoadHook 引入晚加载实现类的场景另外验证引擎真实失败重试。故障注入和测试用 JNI 导出只由 app 测试模块的 Debug CMake 参数显式启用。app 复用同一引擎源目录构建测试副本；库自身的 Debug 与 Release 都默认关闭该选项，独立库构建即使误传开启参数也会拒绝。测试 APK 选择 app 副本，`tools/check-interface-abort-isolation.py` 用 `llvm-nm` 核对两份 AAR 和匹配 Build ID 的未剥离产物，确认不含注入符号，并检查 APK 的 Build ID 来自 app 测试副本。

## Thread 与 GC 压力

测试必须在安装前选择并确认普通/CheckJNI 模式。接口晚加载、嵌套接口与实例/映射失败的覆盖结论见[兼容性文档](COMPATIBILITY.md#已覆盖的场景)。`interface-stress` 的 200 个实现类、四个菱形子接口和两级受影响父类应产生恰好 207 个快照类型；`interface-stress-add` 另外覆盖新增成员。

`InterfaceAbortInstrumentation` 在首次接口事务之前注入普通类线程探测失败，验证错误码 67、`memberMapping` 原因、零共享引用读取与旧定义保留；随后在其他线程保留约 19.7 万个存活对象、持续分配并请求 GC 时交替进行至少 32 次普通类结构重定义；未观测到标记期间读取时有界重试至最多 512 次或 120 秒，覆盖字段和方法增删、旧实例的基本值／引用身份、新字段零值和后续 GC。报告记录 GC 总次数、分配次数、共享引用读取及 CC 标记期间的读取次数；CMC 的最后一项允许为零，GC 压力本身不等同于每次事务都与 CC 标记重叠。`ordinaryGcCycles` 包含 ART 结构事务自身的 GC，不能单独证明后台压力生效。后台通过公开 `Runtime.getRuntime().gc()` 请求 GC；先等待一次调用返回并确认尚未开始事务的区间内 GC 计数增长，再进入重定义循环。`ordinaryPressureGcRequests`／`ordinaryPressureGcReturns` 记录后台请求／返回次数，`ordinaryPressureReadyGcCycles` 记录该准备区间的 GC 增量；请求返回不是独立收集次数，也不是标记重叠证明。

固定 Android 17 的 [System.gc](https://android.googlesource.com/platform/libcore/+/refs/tags/android-17.0.0_r1/ojluni/src/main/java/java/lang/System.java) 对 targetSdk ≤ 34 合并请求，单独调用可能只设置等待 finalization 的标志；[Runtime.gc](https://android.googlesource.com/platform/libcore/+/refs/tags/android-17.0.0_r1/ojluni/src/main/java/java/lang/Runtime.java) 直接进入公开的 native 请求。[Redefiner::Run](https://android.googlesource.com/platform/art/+/refs/tags/android-17.0.0_r1/openjdkjvmti/ti_redefine.cc) 在实例收集与主要映射准备前，通过 `ScopedSuspendAllocations` 主动 GC，并在最终更新前等待、禁止并发移动 GC。因此旧夹具的总 GC 数与零次标记读取不足以认定 Thread 偏移错误。`python3 tools/analyze-runtime-gc-log.py --logcat <raw-log> --device-id <anonymous-id>` 可离线核对已有 CC/CMC 完成日志的进程／线程归属与快照顺序，报告不输出原始进程／线程编号；完成日志仍不能证明标记区间或对象转发。修正后的压力夹具尚待 API 37 真机执行，旧的零重叠结论保留。

## 收集器与 JNI ID 测试 APK

```sh
python3 tools/prepare-collector-apk.py --apk app/build/outputs/apk/debug/app-debug.apk --collector CC
python3 tools/prepare-collector-apk.py --apk app/build/outputs/apk/debug/app-debug.apk --collector CMC
```

工具为离线操作，不安装、连接或预约设备。输入应为未带 ARM64 wrapper、启用 debuggable/extractNativeLibs 的 Debug APK，使用匹配原签名的调试 keystore；可用 `--keystore` 指定。后续设备运行应将生成的 `app.apk` 传给 `--apk`，分别记录包装前后哈希。

收集器测试 APK 可用 `python3 tools/prepare-collector-apk.py --apk <debug.apk> --collector <CC|CMC>` 离线准备，结果与哈希写入 `build/evidence/`。包装脚本默认显式指定 index JNI IDs，移除冲突的 VM 选项及 `--nice-name`：固定 Android 17 的 [app_main.cpp](https://android.googlesource.com/platform/frameworks/base/+/refs/tags/android-17.0.0_r1/cmds/app_process/app_main.cpp) 在 ART 加载前设置进程名，而 [AndroidRuntime.cpp](https://android.googlesource.com/platform/frameworks/base/+/refs/tags/android-17.0.0_r1/core/jni/AndroidRuntime.cpp) 会清零参数区，导致早期收集器选择读不到 `-Xgc`。工具保留原始 APK 载荷和签名证书，不安装或预约设备。运行后仍须按引擎进程核对实际 GC 启动日志，不能仅凭包装参数认定收集器。

默认 `--jni-id-mode indices`。仅对明确的拒绝测试使用 `--jni-id-mode pointer`；在相应测试 APK 上用 `-e scenario interface-pointer-runtime` 检查方法/仅字段接口拒绝 79、旧缓存 JNI 调用与 GC 后行为；用 index 包及 `-e scenario interface-index-runtime` 检查成功对照，包含此前未初始化的仅字段接口。两种 JNI 检查模式均须运行。测试既尝试安装前通过正常 JNI API 生成历史标记，也注入准入结果覆盖目标/子类型，不向已发布对象写入假标记。

GC 日志分析使用 `python3 tools/analyze-runtime-gc-log.py --logcat RAW_LOG --device-id android-device-1`。工具只核对引擎进程、快照顺序和完成日志，不证明标记区间或对象转发，也不应据此把零次标记读取归因为 Thread 偏移错误。

## 固定声明核对

```sh
python3 tools/check-interface-abi-preparation.py
```

该脚本读取 [interface_runtime_table.h](../artdcevm/src/main/cpp/platform/abi/arm64/interface_runtime_table.h)，核对固定声明、arm64 编译断言、实际候选选择与 API 准入。下表描述源码/编译期布局，不是设备通过表；实际结论见[兼容性文档](COMPATIBILITY.md#平台覆盖)。

声明来自 `build/evidence/abi-sources/` 中固定 AOSP 标签的 thread.h、managed_stack.h、runtime_stats.h、handle_scope.h、mirror/class.h、class_status.h、ti_redefine.h/cc；Android 12L 另外核对 android-12.1.0_r1。源码原文、编译输出和逐套运行库报告只在本地忽略目录保存。版本差异只在 `platform/abi/arm64/androidNN/`；相同的 Thread 指针前缀与统计字段复用 common/thread_prefix.h，没有复制后端。

| 版本 | Thread tls32 大小 / tls_ptr 起点 | tid / GC 标记偏移 | JNI env / 顶部 HandleScope 偏移 | HandleScope 头 | ClassRedefinition 末尾标志 |
| --- | --- | --- | --- | --- | --- |
| 11 / API30 | 92 / 160 | 16 / 52 | 216 / 296 | 12，无 size | 四个，末尾 needs_reverify |
| 12、12L / API31–32 | 88 / 152 | 12 / 48 | 208 / 288 | 12，无 size | 四个，末尾 needs_reverify |
| 13 / API33 | 80 / 144 | 12 / 36 | 200 / 280 | 12，无 size | 四个，末尾 lock_acquired |
| 14 / API34 | 84 / 152 | 12 / 36 | 208 / 288 | 12，无 size | 四个，末尾 lock_acquired |
| 15 / API35 | 80 / 144 | 12 / 32 | 200 / 280 | 16，有 size | 三个，末尾 has_virtuals |
| 16 / API36 | 80 / 144 | 12 / 32 | 200 / 280 | 16，有 size | 三个，末尾 has_virtuals |
| 17 / API37 | 80 / 144 | 12 / 32 | 200 / 280 | 16，有 size | 三个，末尾 has_virtuals |

以上数字均由固定声明的 offsetof/sizeof 产生，并由 NDK arm64 编译断言核对；不是从运行库反汇编或内存样本推算。

接口使用的 Class 声明包含 Object 头、class_loader、component_type、dex_cache、ext_data、iftable、name、super_class、vtable、字段数组、methods、access_flags，以及直到 status 的完整后续字段。11–15 为分离的 ifields/sfields，access_flags 为 64、status 为 112；16–17 为合并的 fields，access_flags 为 56、status 为 104。RootVisitor 的虚析构和两个 VisitRoots 重载的顺序在七版一致；RootInfo 仅按引用传递，不读取其字段。ClassStatus 的完整 0–15 枚举在七版及 12L 一致；kErrorResolved 仍是已解析状态，废弃类仍须设为 kErrorUnresolved。

17 的 clinit_thread_id_or_hash 是 atomic 32 位复用槽位：GetClinitThreadId 以高十位是否为零区分 tid 与描述符哈希。android17/Class 声明保留真实槽位名称和按源码定义的 DecodeClinitThreadId；prepared profile 显式传入该字段的 offsetof。没有把它冒充普通 clinit_thread_id，也没有启用该版本。依据：[17 class.h](https://android.googlesource.com/platform/art/+/refs/tags/android-17.0.0_r1/runtime/mirror/class.h)、[17 class-inl.h](https://android.googlesource.com/platform/art/+/refs/tags/android-17.0.0_r1/runtime/mirror/class-inl.h)。

holder 所有版本都是 Handle array、redefinitions vector 指针、initialized/actually_structural/initial_structural 三个 vector<bool>，对应偏移 0、8、16、40、64；libc++ ABI v1 的 vector<bool> 为 24 字节。ClassRedefinition 元素都是 72 字节，driver/klass/DEX 前缀一致，std::string 后 ArrayRef 起点为 48。十三个 Java holder 槽位一致。11 的根遍历线程 ID 参数为 pid_t/int，12 起为 uint32_t；android11 的接口声明记录该差异，公共路径按声明选择两个独立的准确函数原型，缺少对应命名服务时探测失败，不转换到另一种签名。主机测试覆盖两类调用与缺失服务，arm64 编译断言和已有 SO 逐版本核对；实际设备覆盖见兼容性文档。

接口模块的函数 ABI 仍来自公共固定声明：准入 Handle/Class + string*；Run 的 RedefinerPrefix*；Finish 的 ClassRedefinitionPrefix*、holder&、iter*；AllocateNewClassObject 的三个 Handle 与 uint16 class-def index；CreateNewDexCache 的 loader Handle；LinkClass 的 Thread、descriptor、Class/array Handle 与输出 Handle；LookupClass 的 char* 或 string_view；DoLookupResolvedType 的 TypeIndex/referrer 或 DexCache/loader；SetDexCache 的 Class/DexCache；JNIEnvExt 局部引用桥与 JavaVMExt 的全局根解码；ClassStatus 内部写入及 Mark；FindInstanceField 的两个 string_view。ClassExt/数组类别字段继续按 ART 字段表取得，未新增固定偏移。成员映射与校验重定向继续依赖已存在的结构目录和 FindClass 候选声明。

| 源码版本 | 引用读取与标记语义 |
| --- | --- |
| 11、12/12L、13 | `read_barrier_config.h` 的 `kUseReadBarrier` 是编译期选择。`thread.h` 明确将 `is_gc_marking` 定义为 **CC 专用**；`concurrent_copying.cc` 的根翻转回调开启该标志，关闭标记的 checkpoint 将其清除。`ReadBarrier::BarrierForRoot` 在标志为真时调用 `Mark`，否则返回原引用。`Mark` 进入 `ConcurrentCopying::MarkFromReadBarrier`，由 ART 完成标记／转发；该入口也自行检查当前线程的标志。这里没有 CMC/userfaultfd 收集器。 |
| 14、15、16、17 | `is_gc_marking` 的上述含义保持不变，**不是 CMC 的并发标记状态**。`read_barrier_config.h` 允许编译期强制 CC、无读屏障配置或运行时选择；后者由 `mark_compact.cc` 的 `ShouldUseUserfaultfd` 设置 `gUseUserfaultfd`，并令 `gUseReadBarrier = !gUseUserfaultfd`。运行时选择 CC 时仍需 `Mark`；CMC 时 `BarrierForField`／`BarrierForRoot` 跳过 CC 读屏障。CMC 的 `CompactionPause`／线程根翻转更新根，`KernelPreparation` 与缺页处理协调压缩页，不通过 CC 的 `Mark`。 |

源码依据：[11 读屏障](https://android.googlesource.com/platform/art/+/refs/tags/android-11.0.0_r1/runtime/read_barrier-inl.h)、[12L 线程声明](https://android.googlesource.com/platform/art/+/refs/tags/android-12.1.0_r1/runtime/thread.h)、[13 Mark 入口](https://android.googlesource.com/platform/art/+/refs/tags/android-13.0.0_r1/runtime/gc/collector/concurrent_copying-inl.h)、[14 收集器选择与压缩](https://android.googlesource.com/platform/art/+/refs/tags/android-14.0.0_r1/runtime/gc/collector/mark_compact.cc)、[15 读屏障配置](https://android.googlesource.com/platform/art/+/refs/tags/android-15.0.0_r1/runtime/read_barrier_config.h)、[16 收集器约束](https://android.googlesource.com/platform/art/+/refs/tags/android-16.0.0_r1/runtime/gc/heap.cc)、[17 线程与 Mark 语义](https://android.googlesource.com/platform/art/+/refs/tags/android-17.0.0_r1/runtime/gc/collector/concurrent_copying-inl.h)。

## 命名服务审计

```sh
python3 tools/check-structural-symbols.py --report build/evidence/documentation-check/symbols.json
```

工具检查实现对目录的引用、名称所在 ELF 表、目录和实现哈希；不反汇编，不推算地址或字段偏移。准入拼写是候选项；目录总数不代表单台设备必须同时具备全部名称。

ART 私有能力只通过命名 ELF 符号访问，运行时由官方 ShadowHook 按目录声明的表范围查找；`.gnu_debugdata` 也是命名符号表，不涉及反汇编或地址推算。

- [structural_redefine_symbols.h](../artdcevm/src/main/cpp/platform/symbols/structural_redefine_symbols.h)：每套运行库都具备的 22 个当前使用的名称（19 个限定 `.dynsym`，标准 JVMTI 包装器与两个 MethodHandle 入口允许任一命名表）。
- [member_retirement_symbols.h](../artdcevm/src/main/cpp/platform/symbols/member_retirement_symbols.h)：退役访问守卫使用的可变拼写：`Method_invoke` 与 `Constructor_newInstance0` 的无后缀和固定 `.__uniq` 后缀两种拼写、`EncodeFieldId` 新旧两组签名，以及可选的 `MethodHandleInvokeExactWithFrame`（13 起）。
- [verification_redirect_symbols.h](../artdcevm/src/main/cpp/platform/symbols/verification_redirect_symbols.h)：验证重定向使用的 `FindClass` 新旧两种签名。

- [runtime_thread_symbols.h](../artdcevm/src/main/cpp/platform/symbols/runtime_thread_symbols.h)：普通类与接口共用的 `CurrentFromGdb`、`ReadBarrier::Mark` 与可选 `gUseReadBarrier`。
- [interface_redefinition_symbols.h](../artdcevm/src/main/cpp/platform/symbols/interface_redefinition_symbols.h)：接口路径的活动目录。包含普通／结构准入的 `CanRedefineClass`／`GetClassRedefinitionError` 候选、`FinishNewClassAllocations`、`Run`、`HandleScopeVisitRoots`、`EnsureInitialized`、`NewLocalRef`／`DeleteLocalRef`、`CreateNewDexCache`、两个 `DoLookupResolvedType`、两种 `LookupClass`、`SetDexCache`、`JavaVMExt::DecodeGlobal`、`LinkClass`、`SetStatusInternal`，以及可选的 `ThrowIllegalAccessErrorForImplementingMethod` 命名候选。沿用结构目录的 `GetMirrorClass`、`AllocateNewClassObject` 和 `FindInstanceField`。
- [interface_abi_preparation_symbols.h](../artdcevm/src/main/cpp/platform/symbols/interface_abi_preparation_symbols.h)：仅供离线核对的四个历史数组分配名称（含两种带 AllocatorType 的重载）；运行时未解析或调用。准确的 signed/unsigned 根遍历名称在活动接口目录中。

名称存在只说明可以定位；固定声明由运行时探测核对，Hook 覆盖由设备回归确认。

## 诊断报告参考

`DcevmRuntime.capabilities()` 只读取诊断，不执行探测，也不决定请求能否提交。直接调用标准 `RedefineClasses`：`0` 成功；非零时读取错误码与 `lastRejection`。下表保留既有 JSON 键和值的说明，只供排查和测试结束后确认覆盖。`interfaceAddition` 指普通类新增接口，`interfaceRedefinition` 描述接口运行时探测；不要据其中的布尔值或字符串实现接入方的准入开关。

| 键 | 含义 |
| --- | --- |
| `installed`、`standardRedefineRouted` | 基础入口已安装，标准 `RedefineClasses` 已路由到结构事务 |
| `memberAddition`、`memberModifierChange`、`classModifierChange` | 新增成员、成员与类修饰符变化，安装后即为 true |
| `methodDeletion`、`methodSignatureChange`、`interfaceAddition` | 方法退役 Hook 已安装 |
| `fieldDeletion`、`fieldTypeChange`、`memberDeletion`、`memberTypeChange` | 字段守卫也可用 |
| `superclassWidening` | 方法退役 Hook 与校验重定向均已安装 |
| `hierarchyChange`、`fullDcevmComplete` | 始终为 false：收窄不支持，完整 DCEVM 尚未完成 |
| `interfaceRedefinition` | 布尔值；安装且 Class/Thread/HandleScope/holder 全部探测通过时为 true。初始化后尚未探测、版本或服务不可用、进程级探测停用时为 false；单个目标拒绝不清除此值 |
| `interfaceRuntime` | `unavailable`、`unverified: interface runtime declarations`、`verified: androidNN Class/Thread/HandleScope/holder probes` 或 `disabled: 原因`；可用版本仅 API 33–36，其他版本禁用并说明原因 |
| `interfaceAbstractMethodAddition` | 接口运行时与分派探测均通过、必要链接错误服务可用时为 true；每次事务仍须全部实现类验收 |
| `interfaceAbstractDispatch` | 新增 abstract 的只读 IfTable 访问器：`unavailable`、`unverified: ...`、`verified: ...` 或 `rejected: 原因`；方法实际可分派性仍逐事务验收 |
| `interfaceSnapshotTypes` | 当前事务线程最近一次接口快照的类型数（目标、子接口、实现类及其子类） |
| `interfaceSnapshotNanos`、`interfaceAllocationNanos` | 同一线程最近一次快照（含 GetLoadedClasses）和接口分配的单调时钟耗时，单位纳秒；仅供诊断，无接口事务时可为零 |
| `memberMapping` | 安装不可用原因或最近一次探测结果（可能来自 Native Thread 探测）：`unavailable`（可附原因）、`unverified`、`verified: androidNN class layout` 或 `rejected: 原因` |
| `jniRetirementGuards` | JNI 与反射守卫实际拒绝退役目标的次数 |
| `subclassLoadRetries` | 事务期间有目标类的子类加载而重试的次数（见[事务中的类加载](STRUCTURAL_REDEFINE_ENGINE.md#事务中的类加载)） |
| `diagnostic`、`lastRejection` | 安装诊断；最近一次标准入口事务失败时的类与原因，成功时为空。策略、快照与提交前核对的拒绝写明各自原因；ART 自己的检查拒绝时（如 boot 接口在入口即以 79 拒绝），标准入口写明被拒的接口或不可修改的类，否则列出批次中的类与错误名，ART 的原文只在 logcat 的 `FAILURE TO REDEFINE` 之后 |

## 新增版本的验证顺序

1. 两种 JNI 模式先运行 `--scenario-group ordinary` 与失败仪器的 `-e scenario ordinary`。在退役场景执行后确认 `memberMapping` 已完成探测，证明没有把降级当作通过；Android 14 及以上分别核对实际 CC/CMC 配置。
2. 每次只核对一个版本：固定 Class、Thread、HandleScope、holder、ClassRedefinition 标志、IfTable/PointerArray 与命名原型，以及登记表的 `linked_iftable_acceptance`。先做声明/离线核对，不以编译或云端设备目录条目代替执行。
3. 完整接口套件、abort 恢复与 android-compose-hotreload 的 `interface abstract batch` 在真实设备通过后，才单独提交该版本的启用；未取得完整证据的条目保持 `kPreparedOnly`。

## 证据与公开结论

原始日志和逐次设备报告只写入忽略的 `build/evidence/`。在本地 `local/verification/VERIFICATION.md` 记录命令、结论、匿名设备、型号、系统/API 与 APK 哈希；兼容性说明写入 `local/verification/ANDROID_COMPATIBILITY.md`，这两份文档不提交。逐套运行库清单与原始源码/哈希也只留本地。

公开的 [COMPATIBILITY.md](COMPATIBILITY.md) 只接受脱敏精简结论，明确 JNI 模式、GC 证据及未验证范围，不收录原始日志、设备序列号、连接地址、个人路径或邮箱。不要由单次 GC 总数、零次标记读取、离线符号覆盖或故障注入推导超出证据的兼容性。
