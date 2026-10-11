# 基于 ART 结构重定义事务的类演进引擎

状态：方案已确定，Phase 1 实施中。本文取代"调用图选择性解释执行"路线；该路线的代码已备份在本地分支 `backup/call-graph-engine-20261012` 和标签 `backup-call-graph-engine-20261012`。

## 目标与前提

- **目标**：在非 root 的 Android 11–17 / arm64 设备上，于应用进程内实现 VM 级 DCEVM：增删字段和方法、改类型与签名、改修饰符、改继承关系，存活对象保留兼容字段的值。
- **前提**：应用进程**从启动时就是 Java 可调试的**（manifest `debuggable`）。依据 AOSP `openjdkjvmti/art_jvmti.h`：`can_redefine_classes` 属于 `kNonDebuggableUnsupportedCapabilities`，`IsFullJvmtiAvailable()` 只认 `IsJavaDebuggableAtInit()` 或强制纯解释模式，进程启动后再切换调试状态无法获得重定义能力。release 进程不在目标范围内。
- **不做**：不替换系统 ART，不依赖 root，不通过反汇编推算地址或偏移，不自行证明非调试 AOT 帧可以恢复。

## 为什么以 ART 结构重定义为引擎

Android 11 起，`Redefiner::StructurallyRedefineClasses`（JVMTI 扩展 `com.android.art.class.structurally_redefine_classes`）已经实现了 DCEVM 最难的部分：

| 步骤 | ART 已有实现（`ti_redefine.cc`） |
| --- | --- |
| 新类分配 | `FinishNewClassAllocations` 为目标类及**所有已加载子类**按拓扑顺序新建 Class，`AllocateNewClassObject` 从新 DEX 加载并 `LinkClass`，vtable、iftable 和字段布局由 ClassLinker 重新计算 |
| 实例迁移 | `CollectAndCreateNewInstances` 遍历堆，为每个旧实例分配新实例 |
| 字段拷贝 | `UpdateClassStructurally` 按名称和类型拷贝静态字段和实例字段，迁移锁字，清空旧对象 |
| 引用改写 | `Runtime::VisitReflectiveTargets` 改写 JNI ID、反射 Field/Method、MethodHandle、VarHandle 和 DexCache 中的 ArtField*/ArtMethod*；`HeapExtensions::ReplaceReferences` 把堆和根中的旧对象、旧类替换为新的 |
| 执行状态 | `DeoptimizeAllThreadFrames` 让所有帧去优化，`InvalidateAllCompiledCode` 清空 JIT，清空各线程解释器缓存，旧方法标记为 obsolete |

ART 原生只允许"只增不删"，限制集中在少数几个决策函数里。增强库只接管这些决策点，提交、迁移和失效全部复用 ART。这正是旧 Android 13 引擎的方向（在 Android 13 设备上通过了 1025 项检查），本方案把它扩展为覆盖 11–17 的通用引擎。

调用图路线没有继续的原因：真实应用中的反射、JNI、`invoke-polymorphic`、动态代理和 lambda 使静态调用图无法完整，结果只能是经常拒绝；而它试图证明的非调试 AOT 活动帧，在启动时即可调试的进程里不会以应用代码的形式出现。

## 符号证据

以下命名符号在 `assets/runtime_lib` 采集的 27 份设备库中（API 30–37，涵盖 Pixel、小米、三星、vivo、荣耀、华为、一加）**全部位于 `.dynsym`**，可以按名称直接查找，不依赖 `.gnu_debugdata`：

- `libopenjdkjvmti.so`：`Redefiner::RedefineClasses`、`Redefiner::StructurallyRedefineClasses`；`ClassRedefinition::CheckClass`、`CheckFields`、`CheckMethods`、`RecordNewFieldAdded`、`RecordNewMethodAdded`、`CollectNewFieldAndMethodMappings`、`AllocateNewClassObject`、`FinishNewClassAllocations`、`CollectAndCreateNewInstances`、`UpdateClassStructurally`；`HeapExtensions::ReplaceReferences`。
- `libart.so`：`Runtime::VisitReflectiveTargets`、`Instrumentation::DeoptimizeAllThreadFrames`、`jni::JniIdManager::DecodeFieldId`、`DecodeMethodId`。

复核命令：`python3 tools/check-structural-symbols.py`。新的设备库加入 `assets/runtime_lib` 后先运行它；缺少任一符号的 ART 构建只开放公开 JVMTI 能力。

## 接管点

| ART 决策点 | ART 原行为 | 增强库行为 |
| --- | --- | --- |
| `Redefiner::RedefineClasses` | 普通重定义，结构变化一律拒绝 | 路由到原始 `StructurallyRedefineClasses`，保留调用者的 `jvmtiEnv*`、能力检查和 `ClassFileLoadHook`。仅方法体变化时 ART 仍走原地更新路径，行为不变 |
| `CheckClass` | 修饰符、父类、接口不得变化 | 自有策略：允许修饰符和继承关系变化，冲突时（加 final 与已加载子类冲突等）拒绝 |
| `CheckFields` / `CheckMethods` | 只允许新增，修饰符不得变化 | 自有策略：允许删除、类型和签名变化、修饰符变化；有任何结构变化时调用 `RecordNewFieldAdded` / `RecordNewMethodAdded`，确保 ART 走结构路径 |
| `CollectNewFieldAndMethodMappings` | 找不到同名同签名方法时 `CHECK` 失败 | 自有映射：按名称加类型或签名匹配；退役成员映射到**墓碑成员** |
| `UpdateClassStructurally` | 字段拷贝内联了 `CopyFields`，找不到同名同类型字段或父类链不一致时 `CHECK` 失败 | 自有提交，按 ART 源码顺序调用命名的 `VisitReflectiveTargets`、`DeoptimizeAllThreadFrames`、`ReplaceReferences` 等服务；只替换字段拷贝和映射策略 |
| `AllocateNewClassObject` / `FinishNewClassAllocations` | 父类取自旧类 | 父类变化时传入新父类，子类按新层级重建 |

## 语义

- **新增字段**：已有对象取零值或 `null`，不重新执行构造器；新建对象执行新构造器。
- **删除字段、改字段类型、static 与实例互换**：旧字段退役，新字段从零值开始。兼容字段保留原值。
- **删除方法、改签名**：旧方法退役，调用时抛 `NoSuchMethodError`；删除覆写方法后，虚调用按新 vtable 分派到父类实现。
- **退役引用**：`VisitReflectiveTargets` 把退役的 ArtField/ArtMethod 改写为墓碑成员，覆盖反射、MethodHandle、VarHandle 和 DexCache；JNI 字段和方法 ID 只在 `JniIdManager::DecodeFieldId` / `DecodeMethodId` 这两个解码点检查退役状态。不再逐条 Hook JNI、反射和句柄的访问路径。
- **修饰符**：新类直接使用新 DEX 的修饰符。访问收窄后，已解析的旧访问会因 DexCache 被清除而重新做访问检查。
- **继承关系变化**：父类和接口的变化由 ClassLinker 链接新类时处理。收窄（失去某个父类型）前检查：若有不可去优化的帧持有受影响类的实例，返回 `JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED`（66）并保持旧定义，帧退出后可重试。
- **冲突拒绝**：加 final 但已有子类覆写、存活实例将继承未实现的抽象方法等，沿用现有拒绝码，整批不提交。

## 活动帧策略

在启动时即可调试的进程里，应用代码以可调试方式 JIT 编译或解释执行，ART 的 `DeoptimizeAllThreadFrames` 能覆盖全部应用帧。仍然不可去优化的只有 boot 镜像帧；boot 代码是独立编译的，不会内联应用代码，这些帧只**持有引用**。因此：

- 成员变化：不需要额外处理，由 ART 去优化和 obsolete 方法机制保证。
- 继承关系收窄：用 GC 根访问器枚举不可去优化帧持有的引用（栈图为 GC 本来就记录了这些信息），按上节规则拒绝或放行。不解码 CodeInfo、不恢复 vreg。

## 目录落点

遵循现有分层，不新建版本后端：

| 内容 | 位置 |
| --- | --- |
| 结构重定义接管点的命名符号（11–17 共用） | `platform/symbols/structural_redefine_symbols.h` |
| 引擎：前提检查、Hook 安装、路由、策略和提交 | `platform/common/structural_redefine_engine.{h,cc}` 及后续 `platform/common/structural_*` |
| 后端：公开 JVMTI 与引擎组合 | `platform/common/structural_backend.{h,cc}`，由 `platform/backend_factory.cc` 创建 |
| 版本间确有差异的 `ClassRedefinition` 等声明 | 沿用 `platform/abi/arm64/androidNN/`，公共视图在 `platform/abi/arm64/common/` |
| DEX 结构比较 | 沿用 `evolution/` |

## 分阶段计划

| 阶段 | 内容 | 完成判据 |
| --- | --- | --- |
| Phase 1 | 共用符号目录、符号核对工具、可调试前提检查、标准 `RedefineClasses` 路由到结构重定义；新后端接入工厂 | 主机构建通过；Android 16 设备上，独立 Agent 通过标准入口新增字段和方法成功，仅方法体变化行为不变 |
| Phase 2 | 自有 `CheckClass` / `CheckFields` / `CheckMethods` 策略：修饰符变化 | 修饰符变化用例在 Android 16 上提交成功，冲突用例按原错误码拒绝 |
| Phase 3 | 自有映射与提交：字段和方法删除、类型和签名变化、墓碑成员、JNI ID 解码检查 | fixtures 中成员删除和类型变化用例通过，退役引用在反射、JNI、MethodHandle、VarHandle 上抛出约定异常 |
| Phase 4 | 继承关系变化与收窄检查 | 父类和接口的扩展、移除、恢复用例通过；持有受影响实例的 boot 帧导致拒绝且旧定义完整 |
| Phase 5 | 删除调用图模块和启动门控（`native_call_graph*`、`native_selective_interpretation*`、`redefinition_gate`、`production_*` 等），收拢测试入口 | 构建与回归不再依赖这些模块 |
| Phase 6 | Android 11、12、13、14、15、17 逐台回归 | 每台设备的结果、APK 哈希记录在 [VERIFICATION.md](VERIFICATION.md) |

`DcevmRuntime.initialize()` 在 Phase 4 完成并通过设备验收之前继续返回不可用；阶段性能力通过 `capabilities()` 逐项报告，不把部分能力当作完整初始化成功。

## 风险

- **Mainline 更新**：ART APEX 会随 Play 系统更新替换。只在 `ClassRedefinition` 前缀等已有固定声明之上读写字段，被 Hook 函数的其他参数原样传回 ART；每次加入新的设备库都运行符号核对工具。
- **ART 服务语义漂移**：自有 `UpdateClassStructurally` 必须逐版本对照 AOSP 源码的调用顺序；某版本的服务签名不同时，在 `platform/abi/arm64/androidNN/` 增加声明，不复制整套实现。
- **墓碑成员的边界**：墓碑方法和字段的形状必须保证任何调用路径都只会抛出异常，不会按错误布局读写对象。
