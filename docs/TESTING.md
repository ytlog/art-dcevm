# 测试与诊断指南

[English](TESTING.en.md) | 中文

本页说明如何构建、执行测试与核对诊断；已有公开结论见 [COMPATIBILITY.md](COMPATIBILITY.md)，拒绝规则见 [LIMITATIONS.md](LIMITATIONS.md)。测试结束后的诊断用于证明覆盖，不应被接入方用作提交前开关。

## 真实 Android 编辑联调

开发体验验证放在同级 `android-compose-hotreload` 工程，使用正常 Kotlin／Java → D8 → 设备 runtime 路径。先冻结引擎源码，再以独立版本发布 Release AAR 到 Maven Local，避免覆盖公开版本：

```sh
ENGINE_VERSION=0.1.0-dev.$(git rev-parse --short=8 HEAD)
./gradlew :artdcevm:publishToMavenLocal -PpublishVersion="$ENGINE_VERSION"
cd ../android-compose-hotreload
python3 tools/real-edit-test.py --serial DEVICE_SELECTOR --device-id local-apiNN \
  --art-dcevm-version "$ENGINE_VERSION" --scenario-group full
```

`DEVICE_SELECTOR` 是本机选择器占位符，不能写入提交的证据。消费方仅显式指定 `artDcevmVersion` 时从 Maven Local 解析引擎；通常仍使用公开版本。脚本记录匿名设备、源码版本、AAR／APK／引擎哈希，核对实际安装包与本地 AAR 的动态库，原始输出保存在消费方已忽略的 `build/evidence/`。

优先验证保留输入、详情页选择、ViewModel 与独立 `remember` 状态时修改业务逻辑、渲染和已注册 lambda；再验证新增类、批次拒绝与后续恢复。每组连续更新在同一进程执行，并请求 GC、切换前后台。开发期按 `business`、`rendering`、`callbacks`、`recovery`、`compose-state`、`refactoring` 分组定位，冻结后执行双设备 `full`。旧回调改体、插入 lambda、新增捕获与新注册回调分别验收；新回调成功不能代替旧回调通过。

旧回调的捕获兼容由消费方主机处理：保留安装时 D8 桥接及会话内历史入口，转到唯一对应的最新方法体；插入 lambda 不能按编号重配。旧捕获按调试名称和 JVM 类型保留，新增／改名／改型捕获使用 null、零或 false，不重跑初始化表达式、注册工厂或构造器。无法唯一对应或缺少演进所需调试名称时，在设备更新前拒绝，修正后重试。原监听器及引用锚点保持不可变，新建对照回调单独调用；覆盖捕获重排、删除／恢复、同捕获插入及失败事务后重试。详细契约见消费方 `docs/real-edits.zh-CN.md`。这不改变 ART 的结构事务与实例迁移职责。

`compose-state` 检查带 key 的组件／区块重排、动态 LazyColumn 插入／重排／删除，以及各项 remember／rememberSaveable、业务对象、输入、详情与滚动锚点保留；通过实际点击移动后的组件验证事件归属。`refactoring` 使用正常 Kotlin／Java 编译产物，同步改变参数、返回类型、方法名称及顺序，追加返回类、跨文件移动方法，并持续调用同一服务对象；覆盖原子拒绝、修正新追加类后重试与原签名恢复。类追加先于重定义，失败后的既有对象／调用方保持不变不能等同于新类命名空间回滚。排序用例修改顺序数据辅助函数，带 key 的页面作用域保持原槽布局；若 Kotlin 稳定性推断改变了 intrinsic remember 的真实缓存槽布局，对应 UI 作用域仍按上游契约重置，本轮不声称已实现槽迁移。新渲染／重构组用独立活对象触发准入拒绝，原有组仍另测活 ViewModel 拒绝。

该联调不能替代本仓库要求的引擎矩阵，也不表示历史性能越线或长停顿已解决。引擎源码未改时，底层冻结证据保留原始版本与产物来源，不写成新样例 APK 实测。

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

`python3 tools/test-structural-install-host.py` 编译公共生产模块，以公开 JNI/JVMTI 表与 Hook 服务替身验证安装顺序、幂等性、inline hook 失败时不尝试 JNI 覆盖、JNI 表失败后保留基础能力，以及映射名称缺失后线程服务仍准备且诊断保留。测试不执行 ART 对象、收集器或真实 Hook，不能代替设备 ABI 验证。

`--scenario-group retransform` 独立运行 11 个增强场景，`tool` 和完整组也包含它们，准入 arm64 API 30–37（含 12L）。独立 agent 经公开 JVMTI 表调用 RetransformClasses，只在 ClassFileLoadHook 中提供 DEX。核对字段／方法新增、删除与恢复、旧实例兼容值、旧缓存引用持续退役、GC、同内容输出、无替换输出、混合批次的不变类、两目标整批提交、回调内嵌套 retransform、准确的回调／替换次数及失败后的下一事务。无效第二个 DEX 必须回退并返回 60；有效双目标批次在映射计划故障点精确命中一次、返回 110，两个定义及 GC 后行为都须保持，再完成后续退役事务。成功结构变化必须返回 0、无 lastRejection 并观察到真实新成员；原生拒绝或安装能力字段不能代替通过。

`test-structural-install-host.py` 以 73 组测试覆盖三种旧 API 的实际声明步长、公开表匹配、缺少懒展开服务、Hook 顺序／回滚、嵌套成功与异常恢复、整批不变、共享与等内容缓冲的混合批次。不可读保护页验证不读未变化的懒输入、transformer 未读当前映射时的早返回，以及需要时只调用一次命名展开服务；同时核对三次尝试上限、相同完整向量、transformer 和展开不重跑、之后请求恢复。`check-interface-abi-preparation.py` 交叉编译两种布局；`check-structural-symbols.py` 单独报告两个路由入口和仅旧版需要的展开入口。宿主替身、arm64 编译和真实名称均不代替旧设备执行：API 30–32 当前缺少对应设备证据，API 33/36 可用设备承担公共路径回归。

测试 APK 的 app 自有引擎使用 `-O2`，保留 Debug 符号、断言和故障注入；独立 AAR 的构建配置不受影响。`check-interface-abort-isolation.py` 同时核对这些编译条件。已完成场景保存为 UTF-8 JSON 后释放原对象树，避免后续 ART 整堆引用遍历反复扫描报告对象；报告仍包含全部场景与检查，保留原子 `fsync`／rename 发布。

设备 runner 的 `run.json` 记录 `totalSeconds`、各 ADB 阶段的 `commandSeconds`，新 APK 还记录场景与 JVMTI 耗时、检查点耗时及最慢五个场景。场景时间包含场景内检查点，JVMTI 时间是其子集；检查点累计值截止本次发布前，不能将这些值相加。日常先用对应 `--scenario-group` 定向验证，提交前执行适用的完整矩阵。计时不改变返回码、断言、场景数或 30,000 次热调用覆盖。

开发阶段只运行受影响的功能组、生产主机检查和预先选定的性能单元；代码冻结后再执行一次适用的完整验收。失败时先缩到失败单元定位，修复后按影响范围补验，复用已证明未受影响的结果并记录来源、源码及产物哈希。修改共享决策路径时重新评估影响范围；文档修改只需相应文档检查。性能协议在运行前固定，保留失败记录，不追加轮次把越线结果平均掉。

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

`compare-jbr-invoke-kind.py` 与设备组 `invoke-kind` 对照 static／实例双向切换后的旧 static、virtual、super、继承与 range 调用，以及接口 static 改 default 后的旧静态调用，含热调用重编译、调用方与目标活动帧、GC、隐式重建、恢复和失败事务。设备组 `method-private` 还覆盖外部旧 virtual/super/range 的 IAE、接口 default 改 private 的 IAE/NSME、覆写、合法的新自身调用、空接收者、热调用、隐式重建、删除后重加和恢复；JBR 对照用默认、解释和定向编译执行验证自身旧帧。`method-private` 另含八个同批活动调用方场景：两轮反向提交目标与调用方基类，检查 public→private、恢复、规划后失败、旧帧 IAE／旧方法体、GC 与字段保留；目标自身活动帧的缺服务拒绝仍独立检查。`method-private-self` 单独覆盖类与接口 default 副本的旧帧、忽略覆写、range、合法 null 的 NPE、GC、两轮恢复和实际 single-step 回调；有任一选择服务时，第二轮还先更新方法体，验证更早 DEX 的保留，以及另一种类缺少选择服务时的 71。API 33 在独立续接自证明通过时也执行前置方法体更新与真实 private 调用；显式屏蔽续接后仍独立验证 71。`--private-self-mode interface-only|class-only|disabled` 在 Debug 初始化前屏蔽选择入口，测试报告须与屏蔽及逐种类预期一致；此测试不替代 API 34/37 真机验证。 两个入口都屏蔽时，API 36 第二轮热 copied default 返回 71 后，在关闭 single-step 时因 JIT zombie code 重复登记触发 ART 断言；普通／CheckJNI 及独立自身测试进程均可复现。这项压力组合仍受阻，不列作通过证据；复现时保留单步和活动帧保护。设备组 `tool` 单独验证追加子类后的校验与之后再次重定义新增类，覆盖替换类图的一致性。`test-invoke-kind-host.py` 覆盖命名入口的两组 ABI、缺失入口与安装回滚，以及编译标志保留；宿主假服务不替代真实 ART 验证。 独立 `invoke-kind` 进程自行保留 class→interface 拒绝所需的活对象，并检查其 identityHashCode；不依赖完整套件先前产生的实例。

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

ART 私有能力只通过命名 ELF 符号访问，运行时由 Hook 库按目录声明的表范围查找；`.gnu_debugdata` 也是命名符号表，不涉及反汇编或地址推算。

- [structural_redefine_symbols.h](../artdcevm/src/main/cpp/platform/symbols/structural_redefine_symbols.h)：每套运行库都具备的 24 个当前使用的名称（19 个限定 `.dynsym`，标准 JVMTI 包装器、JNI 访问器、两个 MethodHandle 入口与旧帧身份入口允许任一命名表）。
- [constructor_history_symbols.h](../artdcevm/src/main/cpp/platform/symbols/constructor_history_symbols.h)：构造器单个／数组发现入口的两种拼写、调用方与访问检查、标准 JNI 弱引用身份比较。命名比较通过相同／不同对象探针后使用，缺失时回到公开 JNI 表，弱引用仍由 ART 维护。
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

无直接目标实例的自类型数组转换使用 `tools/compare-jbr-self-arrays.py --java-home <fixed-jbr>` 与设备 `--scenario-group self-arrays`。同一 Java 探针检查一至三维数组、保活组件、冷／热缓存、反射／MethodHandle、GC 和恢复；固定 JBR 的跨加载器数组解析及失败批次后恢复异常另记为 `knownOracleDifferences`，不计为对齐。完整设备套件包含该组。

自类型数组叠加 Serializable／Cloneable 父接口交换使用 `compare-jbr-self-arrays.py --java-home <fixed-jbr> --parents` 和设备 `--scenario-group self-array-parents`，并纳入 `self-arrays` 与完整套件。兼容的数组返回视图直接返回，覆盖一至三维的缓存／新反射及 MethodHandle；去掉不兼容视图强转的真实非法 DEX 必须拒绝 62。固定 JBR 的数组首次创建关系残留另记差异；具体范围见[类种类演进](CLASS_KIND_EVOLUTION.md)。

有存活实例的具体类转 abstract 使用 `tools/compare-jbr-live-abstract.py --java-home <fixed-jbr>` 核对固定 JBR 的字段、分派、构造、活动帧、GC、恢复与失败批次。它分别运行解释器和指定热调用方编译的 JIT 模式，并检查转换后的重编译记录。设备 ART 当前仍拒绝 70；布局阻塞和源码实现前提见[存活实例转 abstract](LIVE_ABSTRACT_EVOLUTION.md)。

`proxy-interface` 组的父接口场景包括正反四目标批次顺序、父接口自身的父接口演进、A+B 与仅 A 的两个 Proxy 类、handler Method 身份、缓存 Proxy 构造器、精确收窄引用规则、热分派、GC 与规划后回滚。验证器独立要求对应两轮的行为记录一致，仅排除输入顺序和拒绝原因的目标前缀。

`compare-jbr-inverse-parents.py --java-home <fixed-jbr>` 与设备组 `inverse-parents` 覆盖已有 private/static 声明新增 default/abstract 父接口的四种组合。同批更新父接口、四个子接口及普通类，交换批次顺序，检查两轮恢复、规划失败、缓存成员、JNI、Proxy、共享父路径、后加载类型与 GC。固定 JBR 对缓存槽别名及旧 private 帧的结果另有精确预期；现代 direct 缓存填充或旧版命名访问／历史链服务可用时，设备的八个 private/static 父接口事务均持有四个旧活动帧，覆盖两轮新增／移除和规划后失败；事务期间 GC；private 方法体在新增时从 11 改为 13，恢复为 11，旧帧自身调用、缓存成员及 JNI 必须跟随新方法体，父接口仍独立分派。API 33／36 普通及 CheckJNI 模式验证；缺少 direct 服务的模块仍验证四个活动帧拒绝及无活动帧事务。`inverse-history` 另有 14 个场景（也包含在 `inverse-parents` 与完整套件）：同一组旧帧跨三次成功提交及三次规划后失败，经过新增、移除再新增和 GC 后才释放，必须调用最终的 13 方法体；两轮反向批次后恢复原状态。报告独立要求 waiting/pending、joined、缓存成员及 GC 后 JNI 观察。`privateDirectSelection` 独立报告已安装服务，事务仍另要求布局探测；JBR 的 private 旧帧 ICCE 单独记录，不复制该参考缺陷。`inverse-static` 单独运行八个 public static 父接口活动帧场景，`inverse-parents` 与完整套件也包含它们：两轮反向提交，覆盖新增／移除 default 和 abstract 父接口、规划后失败、旧 copied default 帧自身 static 调用、缓存反射／MethodHandle／JNI、Proxy、共享父路径、字段、GC 与恢复。API36 另关闭两个自身选择入口运行本组，证明它不依赖虚调用选择；缺少 copied-frame 归属保护仍拒绝。固定 JBR 的 static 旧帧调用结果对齐，继承 abstract 后缓存静态成员的槽别名仍作为参考 VM 差异单列，不要求 ART 复制。

`interface-symbols` 还在每轮失败、更新和恢复前后独立验证普通类及接口实例方法的返回／参数加载器别名，要求精确 `IllegalAccessException`，错误返回描述符要求 `NoSuchMethodException`；固定 JBR 工具共享行为夹具，设备报告验证器另行固定预期。

`compare-jbr-layout-contracts.py --java-home <fixed-jbr> [--sdk <android-sdk>]` 在独立解释器／batch 进程固定新父类字段的偏移借用、构造器未执行、兼容字段保留与 GC，以及只读的 Unsafe／FieldUpdater／冷反射偏移行为。它还检查 ClassFile 字段顺序确实不同、冻结 D8 的 DEX 完全相同。batch 运行不作为已编译热点的证据；原始 offset 探针只读扩大后对象内的 primitive 槽，不进行引用 offset 写入。

`interface-symbols` 还逐状态检查子接口 `findStatic` 的继承实例／自身 private／自身 static 选择，错误返回／参数描述符、返回／参数加载器别名和 null 名称。static 活参数证明与原 virtual 证明独立；宿主回归分别调用 Android 11–14 与 15–17 的 PointerSize 声明。

`receiver-loss` 使用与 `compare-jbr-receiver-loss.py --java-home <fixed-jbr> --probe-jni-boundaries` 共享的普通接收者 fixture，覆盖失去接口后的直接／精确／参数转换／精确绑定句柄、缓存类句柄与反射、错误类型／null／方法体异常、保留字段与对象身份、后加载子类、三目标批次反序、两轮规划失败／提交／恢复及 GC。它纳入完整套件。完整套件还要求已绑定 `invokeWithArguments` 的数组／List／null 数组、返回值装箱、spread／drop 适配、捕获 ICCE、基本类型扩宽、引用转换、null 拆箱、错误参数个数与方法体异常。`receiver-loss-bound` 保留为同一完整分组的别名；缺少证据，或把错误异常同时写成 expected／actual，均不能通过独立验证器。固定 JBR 对照使用同一带参数方法及直接静态异常处理方法。JBR JNI 非法接收者探针运行于独立进程，CheckJNI 终止只记诊断；正常 Java 对照的每个热调用阶段均要求实际编译证据。

`tools/test-bound-method-handle-host.py` 另覆盖 69 项公开 JVMTI／原生入口准备与回调行为：能力／元数据不可用、实际入口及局部变量探针、重入、准备与回滚失败后重试、兼容快路、null／非实例／obsolete 目标和已有 JNI 异常的保留；新增原生覆盖探针、具名模板地址合并、服务／标记不可用、同步方法与元数据失败后备、外部事件异常优先及 ART 异常处理转交；还覆盖检查策略的目标回调命中、其他方法或线程命中不能作为证明、原回调保留、清除断点／关闭通知失败、二次覆盖失败及恢复失败后的拒绝和重试。

HookOverheadBenchmark schema 2 新增实际接口 `invokeExact` 和精确绑定调用，独立核对每项返回值校验和。`setup.boundHandleEntryMode` 记录当前实现：0 为公开 JVMTI，1 为 Runnable 事件检查，2 为无自身逐指令通知的帧入口检查，3 为清除自身断点后的检查策略／原生事件路径；基线旧包可无此字段。`setup.unwindFrames`／`unwindEngineFrames`／`unwindAfterEngine` 记录实际 DWARF 回溯计数；现代解释模式必须经过增强库包装帧并继续回溯，禁止在入口处截断。设备驱动拒绝缺少 `methodHandleChecks` 的报告。比较应包含 `engine-client,added,removed,art-added,engine-body,selected`，每状态／模式至少三次新进程。`jit` 表示启用 JIT 并预热，不能单凭模式名声称每个 lambda 都已编译。

Schema 3 另测三维引用数组的 JNI `IsAssignableFrom`，要求 `arrayAssignabilityChecks` 和逐批返回值校验和。验证器数组修复须同时测这条普通运行路径，避免把全局数组判断 Hook 的开销遗漏在反射／MethodHandle 指标之外；比较使用相同测试夹具、签名和资源，仅替换增强库的对照 APK。

Schema 4 支持 `--cpu-affinity <cpu>`，只在 Debug 基准的当前线程内设置亲和性。每项测量前后都核验单核掩码和实际运行 CPU，驱动同时核对顶层与每项指标的证明；丢失绑定则拒绝报告。基线和候选须选择同一 CPU；默认不绑定。固定 CPU 的对照须与原始未绑定结果分别保留，不能据此把原先的越线改写为通过，也不能单凭该对照证明原先波动的精确原因。

跨提交比较先保留原始整包结果。测试类或 DEX 有变化时，即使基准 Java 源码相同，整包差值也不能直接归因于增强库。可用兼容的 Debug 测试引擎构造同一测试包的库替换对照：

```sh
python3 tools/prepare-hook-benchmark-control.py \
  --harness-apk <frozen-current-debug.apk> --engine-apk <frozen-baseline-debug.apk>
python3 tools/test-hook-benchmark-control.py
```

工具只替换 `lib/arm64-v8a/libartdcevm.so`，保留原测试包的 DEX、fixture、其他原生库及 `wrap.sh`（JNI ID 模式）。它移除旧签名、按 16 KB 要求对齐、用 Android Debug 密钥重新签名并核对签名证书，逐项核对除签名外的全部 payload；重复 ZIP 项、相同引擎或额外字节变化均拒绝。APK 与包含输入／输出哈希、相同 payload 哈希的 `report.json` 写入忽略的 `build/evidence/hook-benchmark-control/`，工具不操作设备。这是性能诊断包；引擎与测试 JNI 的兼容性仍须由实际运行证明。

用 `run-hook-overhead-benchmark.py` 比较原测试包和生成的 `control.apk`，事先固定轮数并平衡执行顺序。必要时反向保留旧测试包、放入新库，形成两种测试包 × 两种库的交叉对照。核对安装哈希和原始样本，分别报告整包差值、相同测试包下的库差值及仍未解释的变化。同测试包对照通过不能覆盖原整包超限记录，也不能仅由计时推导具体的代码布局、编译状态或缓存原因。


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

`ReceiverLossAdapters` 将 43 组预链接适配器图同时交给 ART 和固定 JBR；`method_handle_adapter_expectations.py` 为两个报告验证器固定独立结果与回调轨迹。覆盖组合后绑定、忽略／替换接收者、guard 分支、内外层处理器、过滤器抛错、collect／fold、置换、spread／collect、invoker、显式转换及 varargs。`test-captured-method-handle-host.py` 另验 34 项根化续执行、upcall 失败、元数据重试、入口身份与已有异常；入口测试还验证帧返回服务／覆盖缺失、续执行准备及提前返回失败、其他 agent 的强制返回。准备探针必须通过混合引用／基本类型参数复制、引用返回值与恰好一次执行检查。

`boundHandlePreparationProofs` 记录启动时的身份、普通入口、初次原生入口、公开续执行、重复原生入口与原生续执行证明（位 0–5），以及检查策略／帧入口激活（位 6／7）；原生策略／帧入口性能报告分别要求 127／191。首次事务直接移除接口时，也在已证明的 Runnable 探针中运行既有 Class／Thread 校验后再选择原生续执行，不能依赖更早的普通重定义。

`tools/compare-jbr-constructor-history.py --java-home <fixed-jbr>` 在独立解释器／默认 JBR 进程中，用共享构造器探针核对固定预期。设备 `constructor-history` 组覆盖公有／声明共享根的双向独立预热、单个／数组查询副本、错误参数预热、abstract／接口／注解转换、恢复、GC 和失败事务；`test-constructor-history-host.py` 验生产守卫、原始／引用转换、标签释放、弱引用清除／分配失败、线程退出清理，以及缓存命中后 ART 返回非空局部引用但仍挂起目标异常的行为。构造热入口仍逐次核验 ART 弱引用身份及事务 epoch；标签查询、参数适配和异常构造走独立慢路径，TLS 地址缓存仅保存原生元数据的地址。报告验证器的负例要求精确 cause 与完整观察集合。构造活动帧和初始化前／序列化发现仍是独立待补范围。

`python3 tools/run-jni-frame-tests.py --serial DEVICE_SELECTOR --device-id android-device-1 --apk <frozen.apk> --id-mode index` 使用与 JNI 接收者矩阵相同的维度，并分别停住 Native 调用方和已进入的旧 Java 方法体，共 192 个隔离进程。mixed 使用 `-PjniPointerProbe=true` 构建的冻结 APK，并要求实测 tag0／tag1。每轮在接收者合法时先停住四个调用，再依次执行失败／提交／恢复失败／恢复，反序批次并 GC；JNI 局部引用、旧 Java `this`、保留字段和身份须保持。Native 停顿后才分派的调用使用当前接收者边界；旧方法体须继续返回原值，之后的新调用再检查边界。工作线程的公开 JNI 函数槽须与已配置的主线程一致，防止遗漏 CheckJNI 策略。index 的 120 项调用与 72 项原诊断、mixed 的 96 项调用与 72 项原诊断及 24 项独立 79 拒绝分别计数，拒绝不算调用对齐。`--replay` 用同一冻结 APK 重新核验原始观察；证据写入 `build/evidence/jni-frames/`。

`python3 tools/compare-jbr-jni-frames.py --java-home <fixed-jbr>` 复用相同 Java gate、接收者 fixture 和公开 JNI 原生探针，在默认 VM／解释器、普通／CheckJNI 的 192 个新进程中核验活动帧、重新分派、GC 与两轮原子失败。JBR 用损坏的同伴 classfile 触发失败，固定要求 `InternalError`；ART 则在合法批次的映射规划后注入 110，两者仅对照失败原子性。非法 JNI 调用必须触发原 CheckJNI 诊断，不宣称编译活动帧或任意调用图已被穷尽。`test-jni-frame-report.py` 负例拒绝错误帧返回、丢失 GC／线程策略证据、提前或软化的诊断、SIGSEGV 与把 79 写成调用成功。

### API 33 private 续接

`--scenario-group private-continuation` 执行 API 33 的 37 个场景：普通类／接口 copied default、覆写接收者、各种返回类型、普通／range 调用、null、GC、先改方法体、失败回滚、重复恢复、非尾后续计算、catch/finally、锁、递归旧帧、重入、热循环中连续提交，以及 ClassFileLoadHook 在初始扫描后启动旧帧且重扫次数为零。新增的同类重入场景在新 private 方法执行期间再次重定义该类／接口，分别正常返回或抛异常，核对回调只执行一次、旧局部变量、finally、后续调用选中新方法及恢复。其他 API 不执行这个附加组。关联路径定向运行 `method-private-self`、`method-private`、`invoke-kind`、`receiver-loss-bound` 和 `inverse-*` 的普通／CheckJNI 组合。安全拒绝 71、CheckJNI 诊断、真实调用成功分别记录。

`test-retirement-filter-host.py` 验证精确集合对照、饱和及并发发布；`test-interpreter-notifications-host.py` 验证合并租约、部分失败、重入与成功后常驻。`-PcontinuationProfile=true` 开启可选 Debug 阶段计时及命名 DoCall 计数，默认 app 和生产 AAR 不含该 profiler。稀疏嵌套计时有重叠，只作诊断，不能相加或代替性能验收。

运行前固定协议：绑定单 CPU 的一对预检，再按 control/candidate 交替顺序运行六对，不追加轮次。监听期间以真实非尾旧帧自身调用的 ns/call 为指标，预热 10,000 次、测量 100,000 次，成对相对增幅中位数不超过 10%。监听关闭的稳态指标仅在成对增量中位数同时超过 5 ns 与 3% 时失败。前后核对 CPU 绑定，保留无效与越线运行，同设备功能／GC／性能串行。冻结代码后再执行 API 33／API 36 普通／CheckJNI 完整矩阵，定向结果不能替代。

`test-interpreter-events-host.py` 编译生产共享 Hook 所有者，用精确类型替身验证 API 30／31／32 的 instrumentation 指针及 API 33／36 的双 bool 转发、marker 消费、真实异常回放、缺服务／Hook 失败和 ICF 去重，共 30 项。`test-invoke-kind-host.py` 增至 31 项，覆盖精确 direct／range 指令筛选、构造器／字段／外部类拒绝、缺服务及类／接口独立完成证明；`test-dex-code-host.py` 对真实 D8 DEX 核对 MethodId 所有者、同描述符不同加载器及越界拒绝；signed／unsigned root 原型由 `test-interface-scope-visitors-host.py` 分别调用。主机结果不证明旧设备 ABI 可执行。

续接性能基准增加 `-e continuationKind interface`，独立测 copied-default；省略或使用 `class` 测普通类。两种目标各按上述一对预检、六对交替和 10% 门槛验收，不能混合平均。当前先对 API 33／36 完整普通／CheckJNI 回归；API 30–32 没有本功能设备证据，仍保留 71，后续取得设备后须先运行内部证明与缺服务负例，再执行该版本适用的完整验收和同一性能协议。

默认 JIT 稳态基准在入口预热后按约 5 ms 再次校准每批调用数。显式启用 `--require-stable-jni` 时，JIT 改为约 50 ms 批次并持续预热实际批次 2 s；解释模式保留约 5 ms 批次，其 JNI 时钟对照也预热实际批次 2 s。只在预热前校准会使编译后的批次缩至约 1 ms 或更短，21 批分配采样易落入不同 GC 阶段。该修正不增加进程轮次、不剔除 GC 样本，也不改变成对门槛；校准修正前的越线与独立诊断必须保留。

旧设备就绪后，可用 `--scenario-group method-private-self --continuation-proof` 显式请求内部候选证明，并核对随后应用场景仍返回 71。该 Debug 选项不改变设备准入；证明失败、direct 边界未证实或缺服务须保留失败报告，不得记为旧版功能通过。

缺少 throwing direct 入口的替代路径可在 API 33 用 `--direct-pc-mode forced` 验证：准备报告必须为 `dex-pc-direct`，真实完成证明掩码须包含 1／2／8，证明后才发布 `privateDirectSelection`。`--direct-pc-mode disabled --scenario-group method-private-self` 屏蔽原入口并禁用替代，必须在自证明前失败并实际返回 71。API 36 两个测试模式均保留 `modern-selector`，不能用其现代分派结果证明旧版 ABI。冻结后 API 33 默认／强制替代及 API 36 默认路径各跑完整普通／CheckJNI；旧版无设备证据保持关闭。

`-e continuationInvoke direct` 在 private 旧帧跨提交后的计数循环中测普通类／接口 direct 调用。control 与 candidate 使用同一 APK，分别选择原 throwing-access 和强制 DexPc 路径，均先完成自证明，关闭测试环境通知，只保留引擎监听；使用 public 结构变体保留已 private 的 direct 目标。每种目标独立按一对预检、六对交替、CPU 2、预热 10,000／测量 100,000、成对增幅中位数不超过 10% 验收；不能与 virtual 单元混合平均。

JIT 预热现在通过独立 JNI 循环真实调用 workload：三个 `*Mega` 单元用 `run(8)` 覆盖全部八种接收者，其余用 `run(1)`，每段至少 65,536 次方法进入且不少于原 400／200 ms，中间仍暂停 100 ms；计时批次仍在预热后校准，仍为 21 个样本。Java 大批次计时预热及批次上限 64 的尝试都在只读入口诊断中出现绑定 lambda 在计时内切换入口，因此保留失败记录并不进入其验收。独立 JNI 入口预热确认 workload 与绑定 lambda 在计时前切换、计时内入口稳定；不改 ART 访问标志、代码或对象字段。此修正影响全部 JIT 单元，须按新协议重新预检与六对验收；独立续接计时不调用这个预热方法，可按原 APK 哈希及相同引擎／计时源码复用；共享稳态计时库或校准流程变化时须核对解释模式影响，无法证明未受影响时也重跑完整对照。功能完整矩阵也保留原 APK 身份，不能改写为新测试包实测。

固定 CPU 绑定与 sysfs 瞬时频率不能证明有效执行频率稳定。稳态预检越线后，另设固定轮次的诊断，结合工作线程的硬件周期、线程 CPU 时间、GC 与编译入口核对运行条件；外部计数器的窗口须排除诊断序列化和解释执行的等待轮询，并保留剩余采样开销。公共固定性能模式命令返回成功也须实测验证，结束后恢复测试设置。诊断不并入验收，不把旧越线追认成通过；未证明条件稳定时，不启动新的完整对照。

周期／指令计数与线程 CPU 时间必须采用相同起止边界。外部 PMU 的完整窗口可能包含未计入各批 CPU 时间的报告记录、分配和握手开销，不能用全窗口周期除以各批 CPU 时间之和，声称得到精确的有效频率。阻塞握手后的首批变慢也会使全段平均值与批次中位数分离；两种统计量不能互换，更不能据此校正旧预检的中位数。参照只位于整批调用前后时，其稳定性仍不能排除批内有效频率变化。保留全部批次，后续诊断须先证明采样窗口对应关系。

参照全距异常应先分别核对最小值、最大值相对中位数的偏差，区分偏快与偏慢。定位时可同时记录嵌套的线程 CPU／墙钟边界、线程切换和调度等待；这些区间的差值不能直接当作精确的中断耗时。`simpleperf stat --group` 的逐事件读取不保证同一时间边界；支持时，可用 `record --add-counter` 的同次组采样，对齐参照区间内的用户态周期、指令和恒定频率周期，并核对计数器范围、丢样及复用调度。只读取命名计数服务，不分析 ART 指令地址。预先固定边界保护区，所有边界样本也须保存；硬件事件不可用时明确标为缺服务。采样本身会引入中断，IRQ 计数不能自动区分采样器与其他来源，架构计数器和单调时钟也可能共享时钟源。诊断只能解释对应进程，不能追认旧失败的原因或替代不带采样器的稳定性验证。

需要独立时钟校验的稳态对照显式启用 `run-hook-overhead-benchmark.py --require-stable-jni --cpu-affinity <cpu>`；默认保留原入口预热协议，不隐式延长旧设备进程。每个单元开始及计时前固定建立 CPU 绑定；计时期间只校验，不重新绑定或重试。分配无关的 `jniCallIntMethod` 在每批计时外执行独立寄存器工作量参照（4,194,304 次迭代），并核对参照前后绑定、线程 CPU 时间、频率累计驻留和 GC。参照不访问 ART，不根据假定 IPC 推算绝对频率。

分配单元需要独立时钟校验时，使用 `--clock-units methodHandleBound,reflectNewInstance`。每个选定单元先预热实际批次 2 s，再在完整 21 批采样之前、之后各执行 21 次独立参照，不在分配批次之间插入参照。沿用参照中央分布 1%、全距 2%、频率驻留 98% 及成对参照差 1% 的界限；GC 次数和 CPU 时间变化作为被测工作保留，不剔除或修正样本。该选项可配合旧设备的默认短协议使用。选定单元须在运行前固定，之前的失败独立保留。

`hook_benchmark_conditions.py` 对 JNI 校验要求全部 21 批 CPU 时间及 42 个参照值有效，P90–P10／中位数不超过 1%，全距／中位数不超过 2%；频率主档驻留至少 98%，GC 计数不变，JNI 返回校验和须独立符合 `21*N*(N-1)/2`。跨 APK 成对比较还要求同一频率档及参照中位数差不超过 1%，完整对照固定为相应预检控制格的频率档。任何一项不满足则整格无效，原始值全部保留；不归一化或校正原始 ns，不剔除样本，性能门槛和进程轮次不变。`test-hook-benchmark-conditions.py` 的负例覆盖缺服务、非有限值、单个异常值、时钟漂移、计数器重置、GC 和伪造调用结果。


`--foreground` 是可选的测量条件：设备须已解锁且屏幕交互可用，测试窗口须获得焦点，工作线程须实际进入 `/top-app`。启动、退出的主线程操作及窗口就绪等待均有期限；超时使该格失败。就绪时记录进程的 `foregroundEpoch`，每个单元前后检查焦点、调度组和失焦／暂停计数；计数必须始终等于初始值，包括单元之间失焦后恢复也会失败。离线验证要求所有单元与进程初始计数一致。Debug 窗口仅在该进程内保持亮屏，不修改安全锁；跨进程的亮屏条件须由运行器预先固定、记录并在结束后恢复。前台标志本身不证明频率或 JNI 执行稳定，仍须通过原有时钟、GC 与真实返回值校验。缺失窗口／调度服务或锁屏时不回退为可验收的后台计时。

前台运行器等待仪器安装 ActivityMonitor 并发布启动请求后，通过 ADB 显式启动仅用于 Debug 的测试窗口；不依赖应用后台启动权限。启动请求、ADB 返回和窗口就绪均有期限，失败不重试，原始启动输出单独保留。只有实际焦点、调度组及连续性检查通过后才计时。`test-benchmark-foreground.py` 验证未就绪／已结束的仪器、启动拒绝、超时及单次启动约束。 原始设备报告另存于忽略证据目录的 `raw/`。脱敏仅在前台调度组字段保留精确的公共名称 `/top-app`，不得把任意路径放行，也不得把已脱敏的占位符当作调度组证明。

`run-hook-overhead-benchmark.py --summarize` 仅重新汇总完整成功的原运行：必须提供 `run.json`，其预定轮次／模式／状态的每一格和原始报告须全部存在且成功，不能跳过失败格、纳入未登记报告或重复计算同一目录。汇总须显式复用原 CPU、前台、JNI 时钟和选定单元条件，并重新执行与现场相同的报告校验。`test-benchmark-summary.py` 覆盖这些负例；汇总不会追加设备运行或改变原性能判定。

`interface-symbols` 同时覆盖两层父接口和实现类的跨加载器参数／返回类型、错误完整描述符、匹配类型的真实调用及普通反射负例，贯穿失败批次、提交、恢复与 GC。共享 `InterfaceSymbolState` 也用于固定 JBR 的解释器／JIT 对照。`test-method-handle-lookup-host.py` 的 25 组验证另含 1／3／9 层调用链、不同调用方、非 public 声明、分段栈读取失败、缺递归方法标识与缺活参数观察；报告验证器拒绝省略递归检查。此路径改变共享查找守卫，冻结后在 API 33／36 运行完整普通／CheckJNI 功能矩阵。其他运行库的 SO／ABI 分析仅标为静态验证，不能写成设备实测，API 30–32 的续接 71 不变。

`interface-symbols` 另验普通类已声明静态方法的参数／返回类型加载器别名（含两者同时不同）、错误描述符、匹配参数的真实调用、null、返回对象身份及普通反射负例；失败批次、提交、恢复和 GC 前后均必须有证据。共享 JBR 探针使用相同用例。主机回归另验证普通类静态活参数缺失时不发布 static 证明，以及 bootstrap 类、未通过独立证明的非 public 类与不匹配调用方继续委托。

`python3 tools/test-lookup-probe-shrinking.py [--java-home <jdk>]` 用实际 consumer 规则执行 R8，再在 JVM 中分别运行缩减后的 virtual／static／inherited-static／nonpublic-static／bridge／enumeration／static-enumeration／bootstrap-static-enumeration／bootstrap-override-enumeration／bootstrap-instance-enumeration／bootstrap-root-static-enumeration／bootstrap-root-instance-enumeration 探针。它验证字符串查找所需的方法及内部接口层级没有被混淆破坏；仅保留最早的 StaticOwner 不能保护后来增加的探针。该主机结果验证 Java 发布契约，不替代 ART 活帧与设备证明。


`interface-symbols` 另覆盖普通类一层／两层继承静态方法的真实调用、null、对象身份、遮蔽与重载、跨加载器参数／返回类型、错误描述符和声明反射边界。private/protected 访问拒绝、非 public 父类 public 方法、接口 static 不被类继承与方法体异常独立断言。同批 Peer 的 static 方法改体，缓存与新建的子类查找句柄必须随成功事务更新，在失败和恢复后保留正确方法体；每种状态也在 GC 后验证。固定 JBR 共享夹具按解释器和 JIT 模式运行。主机查找保护现有 25 组测试另验缺少 `FindClassMethod`、未观察继承声明、证明异常、boot 声明与公开 ID 映射失败；R8 测试独立调用 virtual、static、inherited-static、nonpublic-static、bridge、enumeration、static-enumeration、bootstrap-static-enumeration、bootstrap-override-enumeration 、bootstrap-instance-enumeration、bootstrap-root-static-enumeration 和 bootstrap-root-instance-enumeration 十二种缩减后证明。冻结后补跑 API 33／36 完整普通／CheckJNI 功能矩阵，性能越线和历史停顿仍须单独记录。

`interface-symbols` 另在每轮失败、提交、恢复和 GC 后验证非 public 普通类的声明／继承静态方法：合法同包调用、null、对象身份、同名类型的加载器身份、错误描述符、普通反射不变，以及 publicLookup／无权限 Lookup／同包名不同加载器 Lookup 的拒绝。跨加载器 Lookup 必须来自实际 alias Class；独立检查 lookupClass 身份。非 public 子类的新旧句柄必须随同批父类改体更新。主机 25 组测试另验非 public 证明无观察、抛异常、只观察声明或只观察继承，以及未扩大到非 public 接口；十二种 R8 缩减后证明（virtual/static/inherited-static/nonpublic-static/bridge/enumeration/static-enumeration/bootstrap-static-enumeration/bootstrap-override-enumeration/bootstrap-instance-enumeration/bootstrap-root-static-enumeration/bootstrap-root-instance-enumeration）分别实际调用。


`interface-symbols` 的协变桥接矩阵使用同一固定 JBR／ART 夹具：普通类、继承类、接口、接口叶子和实现类分别查找 Object 与 String 返回类型，验证完整句柄类型、新建／缓存／invokeExact 调用、覆写接收者、null、目标异常、错误描述符及无权限 Lookup；同批 Peer 改体、失败回滚、恢复与 GC 后重复检查。声明存在性从实际声明类／接口独立枚举；后续公开枚举矩阵另验证继承视图中的最具体声明。主机 25 组另含桥接证明无观察、抛异常、缺类／直接接口／递归接口观察，以及 bootstrap、普通反射和加载器身份保护。R8 独立运行新增 bridge 证明。共享查找路径变更须冻结后完成双设备普通／CheckJNI 全矩阵；原性能与停顿问题单独保留。


`interface-symbols` 的公开枚举矩阵另通过 `getMethods()` 独立寻找完整签名，拒绝重复条目，并检查真实 declaring Class、bridge/synthetic/default 标志。覆盖声明类、继承类、接口叶子、实现类、菱形的两种父接口顺序、类覆写优先、无关方法保留、普通实例覆盖、缓存／新 Method 的真实调用、null／目标异常，以及改体、失败事务、恢复与 GC。此组与固定 JBR 的解释器／JIT 共用夹具。`python3 tools/test-method-enumeration-host.py` 的 110 组生产路径验证覆盖命名入口别名、能力／方法身份／全局根／活参数缺失、四路部分证明、异常、失败安装／解除、重入、类优先、无关／boot／static 范围及类型身份。R8 用实际 consumer 规则运行新增 enumeration 证明。修改共享事务准备和反射入口后，冻结运行 API 33／36 完整普通／CheckJNI；新枚举性能未验收，旧性能与停顿证据不得改标通过。


`interface-symbols` 另验证接口 static 不被 `getMethods()` 继承：声明接口、子接口、多层／两种菱形顺序、直接／间接实现类、同名类 static 隐藏，及普通类继承 static 的真实调用。枚举结果数、真实声明类、`getDeclaredMethods()`、缓存 Method 和接口方法变 static 分别断言；改体、失败／恢复及 GC 后重复。共享固定 JBR 解释器／JIT 同样执行。110 组枚举生产路径主机测试包含新增证明缺失／异常／部分路径、全局根失败、空／混合数组、分配／填充失败及重入，证明失败仍须保留原能力；ASan/UBSan 验证同一生产代码。R8 十二种证明包含独立 static-enumeration 实际调用。冻结后补完整双设备普通／CheckJNI 矩阵；保留基线失败，符号覆盖和历史性能结果不得冒充新包设备／性能验收。


`interface-symbols` 的 bootstrap static 枚举矩阵覆盖 Comparator／Function 的应用子接口、实现类、继承类、两种父接口顺序、应用接口自身 static 与同名类 static。独立检查数量、真实声明类、默认方法及其调用、缓存／新反射调用、null、bootstrap 自身 public／declared 静态调用，以及同批改体、失败／恢复和 GC。固定 JBR 使用相同用例。110 组生产主机测试另验 bootstrap 证明缺失／异常／无观察／部分观察、不可读活根／List、数组失败、重入，以及证明失败仍保留原两项能力；ASan/UBSan 执行相同源码。R8 独立执行第八种 bootstrap-static-enumeration 证明；Java 层不可把 bootstrap 等同于 null 加载器。冻结后完成双设备普通／CheckJNI 全矩阵；保留首轮证明错误导致未启用的失败证据，不计为通过。



`interface-symbols` 另验应用接口覆写 bootstrap 祖先：Supplier 的 Object/String 协变桥接、IntSupplier 普通方法、Comparator 原有 default 的覆写；覆盖声明／叶子／双父序菱形、实现类／继承类、类覆写优先，逐项检查 declaring Class、bridge/synthetic/default 与真实调用。缓存反射、null、标准库自身调用、普通 declared 边界在改体、失败／恢复和 GC 后验证。固定 JBR 共用夹具。110 组生产路径主机测试包含独立 override 证明缺失、四路部分观察、异常、类型身份和活帧失败、重入及不依赖 static 证明的启用；失败须保留之前三项能力。R8 第九种 bootstrap-override-enumeration 探针实际调用。冻结后运行双设备完整普通／CheckJNI，基线失败与原性能／停顿证据单独保留。


`interface-symbols` 另覆盖应用类型继承 bootstrap 接口层级时的最具体声明：List／SortedSet 的 spliterator 和 Path 的 compareTo(Object) 桥接，在子接口／多层子接口／实现类或抽象实现基类上检查声明类、bridge/synthetic/default、缓存及新建 Method 的真实调用。Path 使用真实应用实现类；集合内容与比较结果随同批 Peer 改体、失败／恢复及 GC 验证，另验 null、错误类型、类实现优先及 bootstrap 自身／declared 边界。110 组生产主机模式覆盖四路部分证明、异常、活参数／类型身份失败、重入、缺少旧静态／应用覆写证明时的独立启用，以及失败保留旧能力。R8 第十种 bootstrap-instance-enumeration 探针真实调用；冻结后完成双设备普通／CheckJNI 全矩阵。静态句柄的 caller-sensitive 探索仍未实现，其失败不计入本项通过。

主机另独立验证 bootstrap static 证明缺失而实例证明成功的组合：保留静态原行为，同时启用实例最具体声明修正；修复前该新增用例必须失败，不能由普通全功能通过替代证明隔离。

`interface-symbols` 另覆盖标准库根的接口 static 枚举：UnaryOperator、BinaryOperator、Collator、RuleBasedCollator 检查继承接口 static 为零、自身及继承类 static 的唯一性／声明类／属性、default 保留，以及新建和缓存 Method 的真实调用。共享 Peer 改体、失败／恢复和 GC 后重复，另验 null 比较器异常及 declared 边界；固定 JBR 解释器和 JIT 共用相同夹具。110 组生产主机模式覆盖独立根证明缺失、三路部分观察、Java 异常、活根／List 失败、数组分配／填充失败、重入、前置 bootstrap static 证明缺失，以及缺少实例证明时仍可启用；并验证该证明不会开放 bootstrap 根实例替换。ASan/UBSan 执行相同模式，R8 第十二种 bootstrap-root-static-enumeration 证明实际调用。先运行双设备 interface-symbols 普通／CheckJNI 定向组，冻结后运行双设备完整普通／CheckJNI。API 36 完整功能运行预先安排每个新 instrumentation 进程启动一次惰性前台 Activity，并核对同一 PID，避免此前观察到的后台冻结；不运行基准，也不以此证明性能或停顿问题解决。未修复生产基线的失败和旧性能／停顿证据单独保留。

`interface-symbols` 另验证标准库根继承实例枚举：NavigableSet、AbstractList、AbstractSequentialList、AbstractSet、AbstractCollection 的 spliterator 声明类，以及 List／SortedSet 自身声明、ArrayList／TreeSet 类优先。逐项检查唯一数量、declaring Class、bridge/synthetic/default 和新建／缓存 Method 的真实调用；同批 Peer 改体、失败／恢复与 GC 后重复，另验应用子类覆写、null、错误接收者和普通 declared 边界。固定 JBR 解释器／JIT 共用夹具。110 组生产主机模式包含新根实例证明缺失、无观察／部分观察、异常、写入失败、参数／返回 Class 身份、活根／List／类型失败、重入、前置证明缺失，以及分别缺少根 static、bootstrap static 或全部 static 证明时独立启用且不越权过滤。ASan/UBSan 执行同一生产代码；R8 第十二种 bootstrap-root-instance-enumeration 探针实际调用。先运行双设备 interface-symbols 普通／CheckJNI，再冻结并完成双设备完整普通／CheckJNI。API 36 沿用预先声明的同 PID 惰性前台条件；无新性能测量或停顿解决声明。两份未改生产基线各 240 项声明类失败全部保留。


### Android 16 JIT 代码生命周期回归

`python3 tools/run-jit-lifecycle-regression.py --serial DEVICE_SELECTOR --device-id local-apiNN --engine on` 使用已构建的 Debug APK，在同一 instrumentation 进程中固定执行 32 轮：预热 StringBuilder 与保留的 Hot 对象、增加 Target 字段、更新 Hot 方法体，逐轮校验两个预热校验和及实际调用结果。`--engine off` 是不加载增强库的原生对照；`--fixtures` 可复用已生成且记录哈希的 DEX 目录。运行器核对安装 APK、引擎加载状态、完整轮数和每轮返回值，不能只凭 instrumentation 命令退出码判定通过。原始日志和失败报告保存在忽略的 `build/evidence/`。

API 36 的 ART `RemoveMethodLocked` 非 native 释放路径未同步删除 zombie 记录，旧代码地址被重新分配后可能在下一次失效时触发重复插入断言。最小修复只在命名 `RemoveMethodLocked` 调用的命名 `FreeCodeAndData` 入口、ART 已持有两把 JIT 锁时，清理对应代码的 pending／processed zombie 记录；原断言、释放、成员映射和 GC 均仍由 ART 执行。独立 GC 释放、其他缓存、非释放调用和其他 API 不参与此修复。固定源码声明位于 Android 16 ABI 目录；不通过反汇编推算地址或字段偏移。`python3 tools/test-jit-lifecycle-host.py` 验证释放顺序、地址复用、嵌套／线程隔离、GC 不变及缺命名服务拒绝，并运行 ASan/UBSan。设备混合回归和真实页面连续编辑通过后重新冻结，运行双设备完整普通／CheckJNI 功能及消费者回归；旧失败不得追认为通过。

生命周期边界审查补充：清理必须匹配当前移除的方法，且不能作用于 `FreeCodeAndData` 直接返回的 zygote 共享代码。主机测试还验证这两种排除、嵌套非释放作用域恢复和两个释放线程同时执行。布局验证运行 `python3 tools/check-jit-layout.py --source-manifest SOURCE_AUDIT_JSON --cxx NDK_ARM64_CLANGXX`，manifest 包含固定 `android-16.0.0_r1` 的八份源码路径与哈希；脚本校验内置源码哈希后提取数据成员声明，用 Android arm64 NDK 编译比较尺寸、对齐及字段位置。源码、生成文件和结果留在 `build/evidence/`。这不证明任意厂商或后续 ART APEX 的 ABI；符号存在及 API 级别也不能替代相应运行库验证。
