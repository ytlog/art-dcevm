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

`--scenario-group retransform` 独立运行 11 个增强场景，`tool` 和完整组也包含它们，准入 arm64 API 30–37（含 12L）。独立 agent 经公开 JVMTI 表调用 RetransformClasses，只在 ClassFileLoadHook 中提供 DEX。核对字段／方法新增、删除与恢复、旧实例兼容值、旧缓存引用持续退役、GC、同内容输出、无替换输出、混合批次的不变类、两目标整批提交、回调内嵌套 retransform、准确的回调／替换次数及失败后的下一事务。无效第二个 DEX 必须回退并返回 60；有效双目标批次在映射计划故障点精确命中一次、返回 110，两个定义及 GC 后行为都须保持，再完成后续退役事务。成功结构变化必须返回 0、无 lastRejection 并观察到真实新成员；原生拒绝或安装能力字段不能代替通过。

`test-structural-install-host.py` 以 60 组测试覆盖三种旧 API 的实际声明步长、公开表匹配、缺少懒展开服务、Hook 顺序／回滚、嵌套成功与异常恢复、整批不变、共享与等内容缓冲的混合批次。不可读保护页验证不读未变化的懒输入、transformer 未读当前映射时的早返回，以及需要时只调用一次命名展开服务；同时核对三次尝试上限、相同完整向量、transformer 和展开不重跑、之后请求恢复。`check-interface-abi-preparation.py` 交叉编译两种布局；`check-structural-symbols.py` 单独报告两个路由入口和仅旧版需要的展开入口。宿主替身、arm64 编译和真实名称均不代替旧设备执行：API 30–32 当前缺少对应设备证据，API 33/36 可用设备承担公共路径回归。

测试 APK 的 app 自有引擎使用 `-O2`，保留 Debug 符号、断言和故障注入；独立 AAR 的构建配置不受影响。`check-interface-abort-isolation.py` 同时核对这些编译条件。已完成场景保存为 UTF-8 JSON 后释放原对象树，避免后续 ART 整堆引用遍历反复扫描报告对象；报告仍包含全部场景与检查，保留原子 `fsync`／rename 发布。

设备 runner 的 `run.json` 记录 `totalSeconds`、各 ADB 阶段的 `commandSeconds`，新 APK 还记录场景与 JVMTI 耗时、检查点耗时及最慢五个场景。场景时间包含场景内检查点，JVMTI 时间是其子集；检查点累计值截止本次发布前，不能将这些值相加。日常先用对应 `--scenario-group` 定向验证，提交前执行适用的完整矩阵。计时不改变返回码、断言、场景数或 30,000 次热调用覆盖。

## JBR 对照

`tools/compare-jbr-interface-removal.py` 将同一组 Java 夹具编译为 ClassFile，在固定 JBR `21.0.11+10-b1163.116` 上执行。源码基准为 `cf1f197170a0f04c6559feb873e362583be021d1`，运行时不匹配时拒绝把结果记为该基准的对齐证据：

```sh
python3 tools/compare-jbr-interface-removal.py --java-home JBR_REFERENCE_HOME
python3 tools/compare-jbr-field-retype.py --java-home JBR_REFERENCE_HOME
python3 tools/compare-jbr-proxy-parents.py --java-home JBR_REFERENCE_HOME
python3 tools/compare-jbr-interface-matrix.py --java-home JBR_REFERENCE_HOME
python3 tools/compare-jbr-invoke-kind.py --java-home JBR_REFERENCE_HOME
python3 tools/compare-jbr-interface-symbols.py --java-home JBR_REFERENCE_HOME
```

`compare-jbr-proxy-parents.py` 与设备组 `proxy-interface` 共用父接口夹具，检查共享父接口的保留与移除、旧引用调用与字段清理、恢复、多个接口同时新增且父接口同批重定义、代理类缓存与重复 GC。

`tools/compare-jbr-field-retype.py` 使用同一固定运行时与字段改型夹具，检查普通引用、直接与间接接口、动态代理、继承字段、恢复、重复更新和 GC。

`compare-jbr-interface-matrix.py` 与设备组 `interface-selection` 覆盖 22 种接收者：自身／继承 private、package-private、protected、跨包、分离定义加载器与更具体 public 覆写。分别从新增 abstract、新增 default 和新增父接口开始，切换 abstract/default，并检查旧／后加载调用点、接口反射与缓存 MethodHandle、类调用、30,000 次热调用、GC、后续类加载、两轮恢复及规划后失败。父接口移除还要求旧／后加载的子接口符号调用精确抛 NSME，包含 GC 后调用。设备报告验证器独立固定每种传输、访问标志及加载器关系的期望值。

`compare-jbr-interface-symbols.py` 与设备组 `interface-symbols` 区分子接口的符号所有者与未变化的父接口声明，覆盖移除父接口的 NSME、新增 private/static 遮蔽的 IAE/ICCE、覆写接收者、null 异常优先级、旧／后加载调用、缓存的父声明反射／MethodHandle／JNI 引用、丢失父接口的接收者（反射 IllegalArgumentException、接口句柄 ICCE、类句柄 AME 与父类型转换 CCE）、原有类调用、两轮恢复、GC 与整批失败。JBR 使用解释与定向 JIT 模式并开启 CheckJNI；ART 在成员映射规划后注入失败，要求同批普通类也保持原状态。oracle 与设备均要求新建 `findVirtual` 的完整签名匹配抛 `IllegalAccessException`、错误返回类型抛 `NoSuchMethodException`，同时检查父接口及覆写接收者的新句柄、static 的反射／`findStatic`／unreflect、双加载器的同名参数／返回类型、原访问标志、旧缓存句柄与两轮失败恢复。查找调用方先预热 30,000 次，随后每个状态重复查找 128 次；JBR JIT 日志还须证明新查找调用方实际编译。宿主测试直接编译生产 JNI 查找保护，覆盖能力／符号缺失、局部变量不可读、不同加载器的同名参数／返回类型、延迟安装失败重试和预检撤 Hook 失败与同线程预检重入。

固定 JBR 在新增 abstract 后的接口 JNI 调用可触发 `javaCalls.cpp` 参数数量断言；间接重建类的缓存反射槽也可能别名到另一方法。加上 `--probe-jni-failures` 可在隔离子进程重现新增 abstract 与 default 改 abstract 的参考 VM 失败；后者先证明重定义前的 JNI 调用成功。崩溃单列记录，不计入通过的 JNI 对齐结论。类调用对照使用原字节码及符号查找的 MethodHandle，不能据此声称上述 JBR 缺陷已修复。设备矩阵另外验证新增成员的 JNI 和原类反射调用保持安全与正确。

`JBR_REFERENCE_HOME` 是待替换的路径占位符。原始输出、输入夹具／执行器／运行时哈希保存在忽略的 `build/evidence/`。该对照覆盖特定接口收窄与引用字段规则，不能证明全部 JBR 功能已对齐。ART 设备测试应复核相同返回值、错误、引用清理及失败后行为。

`compare-jbr-member-descriptors.py --java-home <fixed-jbr>` 与设备组 `member-descriptors` 共用接口方法 synthetic／bridge／varargs／strict 及接口／普通类字段 synthetic 的行为断言。六种变体执行两轮更新与恢复，检查新旧反射、旧与后加载调用点、MethodHandle、旧新实例、引用值与 GC。JBR 对损坏的同批成员验证精确 InternalError；ART 在正反两个方向注入映射规划后故障，要求返回 110、命中一次且两项定义均保留。48 个设备场景也包含在 `full` 中。

`compare-jbr-invoke-kind.py` 与设备组 `invoke-kind` 对照 static／实例双向切换后的旧 static、virtual、super、继承与 range 调用，以及接口 static 改 default 后的旧静态调用，含热调用重编译、调用方与目标活动帧、GC、隐式重建、恢复和失败事务。设备组 `method-private` 还覆盖外部旧 virtual/super/range 的 IAE、接口 default 改 private 的 IAE/NSME、覆写、合法的新自身调用、空接收者、热调用、隐式重建、删除后重加和恢复；JBR 对照用默认、解释和定向编译执行验证自身旧帧。`method-private-self` 单独覆盖类与接口 default 副本的旧帧、忽略覆写、range、合法 null 的 NPE、GC、两轮恢复和实际 single-step 回调；有任一选择服务时，第二轮还先更新方法体，验证更早 DEX 的保留，以及另一种类缺少选择服务时的 71。API 33 无选择服务时跳过这项前置 default 方法体更新（ART obsolete 副本检查阻碍），其自身 private 变化继续验证 71。`--private-self-mode interface-only|class-only|disabled` 在 Debug 初始化前屏蔽选择入口，测试报告须与屏蔽及逐种类预期一致；此测试不替代 API 34/37 真机验证。 两个入口都屏蔽时，API 36 第二轮热 copied default 返回 71 后，在关闭 single-step 时因 JIT zombie code 重复登记触发 ART 断言；普通／CheckJNI 及独立自身测试进程均可复现。这项压力组合仍受阻，不列作通过证据；复现时保留单步和活动帧保护。设备组 `tool` 单独验证追加子类后的校验与之后再次重定义新增类，覆盖替换类图的一致性。`test-invoke-kind-host.py` 覆盖命名入口的两组 ABI、缺失入口与安装回滚，以及编译标志保留；宿主假服务不替代真实 ART 验证。

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

`ordinary` 保留普通类新增接口，只跳过接口目标；用于接口尚未启用的版本。`interface-abstract` 便于定位新增 abstract 的问题，`thread-subclass` 单独运行应用 `Thread` 子类场景，`interface-removal` 运行接口移除、重复重建、抽象父类晚加载、JBR 引用字段清理与恢复场景，`proxy-interface` 单独运行 Proxy 实现类场景，`interface-drop-parent` 单独运行接口移除父接口场景，`static-narrow` 检查静态字段的 JBR 复制规则，`field-retype` 检查字段改类型；这些都不能替代完整回归。`--timeout` 调整仪器超时，`--evidence-root` 必须位于忽略的 `build/evidence/` 下。报告须覆盖全部预期场景、错误码、旧定义与后续操作；不能只看仪器进程的退出码。

`--scenario-group interface-shadow` 验证 JBR 的接口／类方法选择：19 个场景覆盖 static/private default、protected/package/跨包类方法 IAE、反射/MethodHandle/JNI/字节码、30,000 次热循环、晚加载子类及 public 覆写、private 父类继承的 AME、恢复与再次添加、abstract/default 切换、新父接口 default、映射计划故障回滚。`interface-abstract` 另验 private AME 与 package/protected IAE，包含 JNI、MethodHandle、字节码和 GC。`compare-jbr-interface-selection.py --java-home "$ART_DCEVM_JBR_HOME"` 运行固定 JBR 21.0.11+10-b1163.116 对照，编译相同 fixture 并应用相同 classfile 访问标志配方。`test-linking-dex-host.py` 独立校验私有 DEX 的声明、只移除接口列表、SHA-1、Adler32 和 map。新选择路径的设备记录目前为 API 33/36 的普通与 CheckJNI 模式。

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
| `interfaceRemoval` | 方法退役、发布前分派保护、实例引用字段检查和 ClassPrepare 快照监视均可用；普通类可以移除接口 |
| `interfaceDispatchSealing` | `available: ...` 或 `unavailable: 原因`；记录经实际探测的发布前链接边界及 canonical 来源，还需要命名分配服务与类加载器分配器读取 |
| `privateSelfSelectionClass`、`privateSelfSelectionInterface` | 对应种类的命名自身调用选择服务及依赖已安装；事务另需布局探测 |
| `privateSelfSelection` | 前两项交集，保留旧诊断语义 |
| `privateSelfSelectionClassReason`、`privateSelfSelectionInterfaceReason` | 服务可用时为空，否则说明不可用原因 |
| `hierarchyChange`、`fullDcevmComplete` | 始终为 false：父类收窄不支持，完整 DCEVM 尚未完成 |
| `interfaceRedefinition` | 布尔值；安装且 Class/Thread/HandleScope/holder 全部探测通过时为 true。初始化后尚未探测、版本或服务不可用、进程级探测停用时为 false；单个目标拒绝不清除此值 |
| `interfaceRuntime` | `unavailable`、`unverified: interface runtime declarations`、`verified: androidNN Class/Thread/HandleScope/holder probes` 或 `disabled: 原因`；准入 API 30–37；源码对齐的版本（API 30–32、37）探测通过后追加 `; declarations not yet device-validated`；其他版本禁用并说明原因 |
| `interfaceSnapshotTypes` | 当前事务线程最近一次接口快照的类型数（目标、子接口、实现类及其子类） |
| `interfaceSnapshotNanos`、`interfaceAllocationNanos` | 同一线程最近一次快照（含 GetLoadedClasses）和接口分配的单调时钟耗时，单位纳秒；仅供诊断，无接口事务时可为零 |
| `memberMapping` | 安装不可用原因或最近一次探测结果（可能来自 Native Thread 探测）：`unavailable`（可附原因）、`unverified`、`verified: androidNN class layout` 或 `rejected: 原因` |
| `jniRetirementGuards` | JNI 与反射守卫实际拒绝退役目标的次数 |
| `subclassLoadRetries` | 事务期间有目标类的子类加载而重试的次数（见[事务中的类加载](STRUCTURAL_REDEFINE_ENGINE.md#事务中的类加载)） |
| `diagnostic`、`lastRejection` | 安装诊断；最近一次标准入口事务失败时的类与原因，成功时为空。策略、快照与提交前核对的拒绝写明各自原因；ART 自己的检查拒绝时（如 boot 接口在入口即以 79 拒绝），标准入口写明被拒的接口或不可修改的类，否则列出批次中的类与错误名，ART 的原文只在 logcat 的 `FAILURE TO REDEFINE` 之后 |

## 新增版本的验证顺序

1. 两种 JNI 模式先运行 `--scenario-group ordinary` 与失败仪器的 `-e scenario ordinary`。在退役场景执行后确认 `memberMapping` 已完成探测，证明没有把降级当作通过；Android 14 及以上分别核对实际 CC/CMC 配置。
2. 每次只核对一个版本：固定 Class、Thread、HandleScope、holder、ClassRedefinition 标志、IfTable/PointerArray 与命名原型。先做声明/离线核对，不以编译或云端设备目录条目代替执行。
3. 固定声明核对完毕，且该版本全部已存设备运行库都提供所需命名服务（`check-interface-abi-preparation.py`）后，可以 `kSourceAligned` 准入；完整接口套件、abort 恢复与 android-compose-hotreload 的 `interface abstract batch` 在真实设备通过后，才单独提交改为 `kDeviceVerified`。

## 证据与公开结论

原始日志和逐次设备报告只写入忽略的 `build/evidence/`。在本地 `local/verification/VERIFICATION.md` 记录命令、结论、匿名设备、型号、系统/API 与 APK 哈希；兼容性说明写入 `local/verification/ANDROID_COMPATIBILITY.md`，这两份文档不提交。逐套运行库清单与原始源码/哈希也只留本地。

公开的 [COMPATIBILITY.md](COMPATIBILITY.md) 只接受脱敏精简结论，明确 JNI 模式、GC 证据及未验证范围，不收录原始日志、设备序列号、连接地址、个人路径或邮箱。不要由单次 GC 总数、零次标记读取、离线符号覆盖或故障注入推导超出证据的兼容性。

类种类转换使用 `tools/compare-jbr-class-kinds.py --java-home <fixed-jbr>` 先测固定 JBR，再运行设备的 `--scenario-group class-kinds`；矩阵、已有实例与子类型前提、缓存调用及原生分配边界见[类种类演进](CLASS_KIND_EVOLUTION.md)。

有存活实例的具体类转 abstract 使用 `tools/compare-jbr-live-abstract.py --java-home <fixed-jbr>` 核对固定 JBR 的字段、分派、构造、活动帧、GC、恢复与失败批次。它分别运行解释器和指定热调用方编译的 JIT 模式，并检查转换后的重编译记录。设备 ART 当前仍拒绝 70；布局阻塞和源码实现前提见[存活实例转 abstract](LIVE_ABSTRACT_EVOLUTION.md)。

`proxy-interface` 组的父接口场景包括正反四目标批次顺序、父接口自身的父接口演进、A+B 与仅 A 的两个 Proxy 类、handler Method 身份、缓存 Proxy 构造器、精确收窄引用规则、热分派、GC 与规划后回滚。验证器独立要求对应两轮的行为记录一致，仅排除输入顺序和拒绝原因的目标前缀。

`compare-jbr-inverse-parents.py --java-home <fixed-jbr>` 与设备组 `inverse-parents` 覆盖已有 private/static 声明新增 default/abstract 父接口的四种组合。同批更新父接口、四个子接口及普通类，交换批次顺序，检查两轮恢复、规划失败、缓存成员、JNI、Proxy、共享父路径、后加载类型与 GC。固定 JBR 对缓存槽别名及旧 private 帧的结果另有精确预期；设备活动帧拒绝场景仅证明失败原子性，不计为活动帧功能已对齐。

`interface-symbols` 还在每轮失败、更新和恢复前后独立验证普通类及接口实例方法的返回／参数加载器别名，要求精确 `IllegalAccessException`，错误返回描述符要求 `NoSuchMethodException`；固定 JBR 工具共享行为夹具，设备报告验证器另行固定预期。

`compare-jbr-layout-contracts.py --java-home <fixed-jbr> [--sdk <android-sdk>]` 在独立解释器／batch 进程固定新父类字段的偏移借用、构造器未执行、兼容字段保留与 GC，以及只读的 Unsafe／FieldUpdater／冷反射偏移行为。它还检查 ClassFile 字段顺序确实不同、冻结 D8 的 DEX 完全相同。batch 运行不作为已编译热点的证据；原始 offset 探针只读扩大后对象内的 primitive 槽，不进行引用 offset 写入。

`interface-symbols` 还逐状态检查子接口 `findStatic` 的继承实例／自身 private／自身 static 选择，错误返回／参数描述符、返回／参数加载器别名和 null 名称。static 活参数证明与原 virtual 证明独立；宿主回归分别调用 Android 11–14 与 15–17 的 PointerSize 声明。

`receiver-loss` 使用与 `compare-jbr-receiver-loss.py --java-home <fixed-jbr> --probe-jni-boundaries` 共享的普通接收者 fixture，覆盖失去接口后的直接／精确／参数转换／精确绑定句柄、缓存类句柄与反射、错误类型／null／方法体异常、保留字段与对象身份、后加载子类、三目标批次反序、两轮规划失败／提交／恢复及 GC。它纳入完整套件。完整套件还要求已绑定 `invokeWithArguments` 的数组／List／null 数组、返回值装箱、spread／drop 适配、捕获 ICCE、基本类型扩宽、引用转换、null 拆箱、错误参数个数与方法体异常。`receiver-loss-bound` 保留为同一完整分组的别名；缺少证据，或把错误异常同时写成 expected／actual，均不能通过独立验证器。固定 JBR 对照使用同一带参数方法及直接静态异常处理方法。JBR JNI 非法接收者探针运行于独立进程，CheckJNI 终止只记诊断；正常 Java 对照的每个热调用阶段均要求实际编译证据。

`tools/test-bound-method-handle-host.py` 另覆盖 30 项公开 JVMTI 入口准备与回调行为：能力／元数据不可用、实际入口及局部变量探针、重入、准备与回滚失败后重试、兼容快路、null／非实例／obsolete 目标和已有 JNI 异常的保留。

HookOverheadBenchmark schema 2 新增实际接口 `invokeExact` 和精确绑定调用，独立核对每项返回值校验和；设备驱动拒绝缺少 `methodHandleChecks` 的报告。比较应包含 `engine-client,added,removed,art-added,engine-body,selected`，每状态／模式至少三次新进程。`jit` 表示启用 JIT 并预热，不能单凭模式名声称每个 lambda 都已编译。


## 接收者失去接口后的保留 JNI ID

普通 Debug APK 用于 `--id-mode index`。另用 `./gradlew -PjniPointerProbe=true :app:assembleDebug` 构建 `--id-mode mixed` 专用 APK，运行前分别冻结 APK。这个显式启用的 Debug [wrap 脚本](https://developer.android.com/ndk/guides/wrap-script) 使用 swapable IDs 并关闭自动提升。仅供 Debug 的探针加载公共 JVMTI 客户端、按要求在附加前缓存 ID，通过公开 JVMTI 挂起其他 Java 线程，再调用固定命名的 `Runtime::SetJniIdType` 服务；复用 ART 自身指针转索引的测试流程，不修改偏移或标志，不进入两份库 AAR。

```sh
python3 tools/run-jni-receiver-tests.py --serial DEVICE_SELECTOR --device-id android-device-1 \
  --apk <frozen-pointer-debug.apk> --id-mode mixed
python3 tools/run-jni-receiver-tests.py --serial DEVICE_SELECTOR --device-id android-device-1 \
  --apk <frozen-debug.apk> --id-mode index
```

每次运行 96 个独立进程，组合附加前／后缓存、仅 default／自身实现、接口／类查找、virtual／nonvirtual、普通／V／A 及普通／CheckJNI。mixed 必须实测提升前标签 0、提升后标签 1：48 项完整调用、36 项原 CheckJNI 诊断、12 项单列的 79 拒绝。index 的两种缓存时机都要求标签 1：60 项调用、36 项诊断。79 验证整批失败原子性、旧调用／身份／字段、GC 与后续同伴事务成功，绝不计作接收者失去接口后的调用对齐。调用项覆盖两轮规划失败／提交／恢复、批次反序和 GC；CheckJNI 项在故意非法调用前持久记录成功事务，再要求原接收者或 nonvirtual 声明类参数诊断与 SIGABRT。旧 DEX 缓存断言、其他崩溃、缺少观察项、ID 标签错误或同时篡改 expected／actual，均须判为失败。

每次启动前记录设备时间，以 PID 和启动时间共同限定 logcat；PID 复用不得把旧进程的诊断计入本次结果。原始报告和日志只写入已忽略的 `build/evidence/jni-receiver/`。使用相同 `--apk`／`--id-mode` 以及 `--replay <evidence-directory>` 可独立重验全部原始记录，另写 `audit.json`，保留原运行记录。接受生产改动前须运行两台设备的这些组合、普通／CheckJNI 完整套件及独立失败恢复。固定 JBR 对照覆盖普通／V／A／nonvirtual JNI 边界，不证明 ART 的指针／索引行为。
