# 基于 ART 结构重定义事务的类演进引擎

状态：Phase 1–2 已实现，并在 2026-10-12 的一台 Android 16 / API 36、arm64 设备上通过指定的 12 次标准 JVMTI 提交与行为检查，见 [VERIFICATION.md](VERIFICATION.md)。Phase 3/4 与其他版本设备回归尚待实施。本文取代"调用图选择性解释执行"路线；该路线的代码已备份在本地分支 `backup/call-graph-engine-20261012` 和标签 `backup-call-graph-engine-20261012`。

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

以下 17 个命名符号在 `assets/runtime_lib` 采集的 27 份设备库中（API 30–37，涵盖 Pixel、小米、三星、vivo、荣耀、华为、一加）**全部位于 `.dynsym`**，可以按名称直接查找：

- `libopenjdkjvmti.so`：`Redefiner::RedefineClasses`、`Redefiner::StructurallyRedefineClasses`；`ClassRedefinition::CheckClass`、`CheckFields`、`CheckMethods`、`RecordNewFieldAdded`、`RecordNewMethodAdded`、`CollectNewFieldAndMethodMappings`、`AllocateNewClassObject`、`FinishNewClassAllocations`、`CollectAndCreateNewInstances`、`UpdateClassStructurally`；`HeapExtensions::ReplaceReferences`。
- `libart.so`：`Runtime::VisitReflectiveTargets`、`Instrumentation::DeoptimizeAllThreadFrames`、`jni::JniIdManager::DecodeFieldId`、`DecodeMethodId`。

标准 JVMTI 表实际使用 `JvmtiFunctions::RedefineClasses` 包装器（`OpenjdkJvmTi.cc`），它在 27 套库中均位于 `.gnu_debugdata`，不在 `.dynsym`。Android 16 实测中，只 Hook 导出的 `Redefiner::RedefineClasses` 没有增强标准调用；优化后的包装器不能假设会经过该导出入口。因此直接按包装器的完整命名符号查找并 Hook，同时核对找到的地址与公开 JVMTI 表一致。`symbols::Find()` 对这个明确列出的符号使用官方 ShadowHook 的 `shadowhook_dlsym()`，其余符号只查 `.dynsym`；`.gnu_debugdata` 仍是命名 ELF 符号表，不涉及反汇编或地址推算。查找规则见 [ShadowHook 2.0.1 手册](https://github.com/bytedance/android-inline-hook/blob/v2.0.1/doc/manual.md)。

复核命令：`python3 tools/check-structural-symbols.py`，共核对 18 个名称及各自的符号表范围。新的设备库加入 `assets/runtime_lib` 后先运行它；缺少 MVP 所需任一符号时增强库初始化失败。

## 接管点

| ART 决策点 | ART 原行为 | 增强库行为 |
| --- | --- | --- |
| 公开表的 `JvmtiFunctions::RedefineClasses` | 经 `Redefiner::RedefineClasses` 的普通重定义，结构变化一律拒绝；导出入口可能被内联 | 路由到原始 `Redefiner::StructurallyRedefineClasses`，保留调用者的 `jvmtiEnv*`、能力检查和 `ClassFileLoadHook`。仅方法体变化时 ART 仍走原地更新路径，行为不变 |
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

遵循现有分层，不新建版本后端。`artdcevm/src/main/cpp` 下：

| 内容 | 位置 |
| --- | --- |
| JVMTI Agent 入口与 `DcevmRuntime` 的 JNI 导出 | `core/agent.cc` |
| 接管点命名符号（11–17 共用）；`Find()` 用官方 ShadowHook（Maven prefab）按目录指定的 ELF 符号表范围查找 | `platform/symbols/structural_redefine_symbols.h`、`art_symbol_compat.h` |
| 11–17 共用的 `ClassRedefinition` / `Redefiner` / `DexFile` 前缀声明 | `platform/abi/arm64/common/redefinition.h`；确有版本差异的声明放 `platform/abi/arm64/androidNN/` |
| 引擎：前提检查、Hook 安装、路由、校验 Hook、能力报告 | `platform/common/structural_redefine_engine.{h,cc}` |
| 校验策略（纯函数，主机可测） | `platform/common/structural_policy.{h,cc}` |
| 用公开 JVMTI 读取旧类结构 | `platform/common/schema_snapshot.{h,cc}` |
| 读取 ART 已映射 DEX 中的类声明 | `evolution/class_schema.h`、`evolution/dex_schema.cc` |

## Phase 2 校验规则

`CheckClass` 时读取新 DEX（`self->dex_file_`，ClassDef 0）和旧类（公开 JVMTI），交给 `structural_policy` 评估；结果按 `ClassRedefinition*` 缓存在线程局部，供随后的 `CheckFields` / `CheckMethods` 使用。拒绝时只写 `driver_->result_` 并返回 false（`Redefiner::RecordFailure` 本身也只做这件事和格式化消息），原因写入引擎诊断和 logcat。接受且有新增成员或修饰符变化时调用 `RecordNewFieldAdded` / `RecordNewMethodAdded`，让 ART 走结构路径，新类采用新 DEX 的修饰符。

成员身份：字段为（名称、类型、static），方法为（名称、描述符）。比较前把 DEX 原始标志规范化为 JVMTI 视图（与 `ti_method.cc`、`ti_field.cc`、`ti_class.cc` 一致：取 Java 标志位，方法的 declared-synchronized 映射为 synchronized，abstract 方法去掉 native）。

| 变化 | Phase 2 结果 |
| --- | --- |
| 新增字段、方法 | 接受 |
| 已有成员的访问级别、final、volatile、transient、synchronized 变化；Java → native | 接受 |
| 方法新增 final，且已加载的子类覆写它 | 拒绝，`METHOD_MODIFIERS_CHANGED` |
| native → Java，abstract 相关变化 | 拒绝，`METHOD_MODIFIERS_CHANGED` |
| 删除字段，字段类型或 static 属性变化 | 拒绝，`SCHEMA_CHANGED`（Phase 3 支持） |
| 删除方法，方法签名变化 | 拒绝，`METHOD_DELETED`（Phase 3 支持） |
| 类修饰符变化 | 拒绝，`CLASS_MODIFIERS_CHANGED`（Phase 4 支持） |
| 父类、接口变化 | 拒绝，`HIERARCHY_CHANGED`（Phase 4 支持） |

## MVP 引擎实现流程（`structural_redefine_engine.cc`）

接口已在 `structural_redefine_engine.h` 定义：`Install(JavaVM*, jvmtiEnv*)` 与 `Capabilities()`，由 `core/agent.cc` 调用。

1. **Install**（幂等，由 agent 的互斥锁串行化）：
   1. 用自有 jvmtiEnv 的潜在能力确认 `can_redefine_classes`，否则失败。
   2. 先通过 `hooks::Initialize()` 初始化官方 ShadowHook，再通过 `symbols::Find()` 解析 MVP 用到的 7 个符号：公开表的 `JvmtiFunctions::RedefineClasses`、结构入口、三个校验点、两个 `RecordNew*Added`。任一缺失就失败，并写明缺失的符号；公开表与命名包装器不一致时也失败。
   3. 保存结构入口和三个校验点的原始地址；通过 `hooks::Install()` 调用 `shadowhook_hook_sym_addr` 替换标准包装器和三个校验点，标准包装器最后安装。失败信息取 `shadowhook_to_errmsg`；任一步失败，通过 `hooks::Remove()` 调用 `shadowhook_unhook` 逆序撤销已安装的部分。
2. **标准入口**：
   1. 进入 ART 之前，线程仍处于 Native 状态，此时对每个目标类：用 `live_schema::ReadLiveSchema` 读取旧声明，并用 JVMTI 收集已加载子类声明的方法。
   2. 按 `jclass` 存入线程局部批次上下文（ART 的 `ClassRedefinition::klass_` 就是同一个 `jclass` 值）。
   3. 原样调用原始 `StructurallyRedefineClasses`，返回后清理上下文（RAII），返回 ART 的结果。

   不在 `Check*` 中调用 JNI/JVMTI，因为它们运行在 ART 的 `ScopedObjectAccess` 内。
3. **CheckClass**：
   1. 按 `redefinition.h` 取出 `klass` 和 `dex_file`。上下文中找不到该类时，调用原函数。
   2. 由 `DexFilePrefix` 构造 `DexView`（文件大小取自 DEX 头），用 `ReadClassDeclarations` 读取新类。读取失败或描述符不一致时，调用原函数。
   3. 调用 `structural_policy::Evaluate`：拒绝时写 `driver->result`，记录 `lastRejection` 和 logcat，返回 false；接受时按 `ClassRedefinition*` 在线程局部保存决策，返回 true。
4. **CheckFields / CheckMethods**：有对应决策时，按 `fields_structural` / `methods_structural` 调用 `RecordNewFieldAdded` / `RecordNewMethodAdded`，然后返回 true，`CheckMethods` 之后清除决策；没有决策时调用原函数。
5. **Capabilities**：报告 `installed`、`standardRedefineRouted`、`memberAddition`、`memberModifierChange`；`memberDeletion`、`memberTypeChange`、`hierarchyChange`、`fullDcevmComplete` 为 false；外加 `diagnostic` 和 `lastRejection`（用 `support/json.h` 转义）。

MVP 不接管映射、类分配和提交（Phase 3/4）。

## 分阶段计划

| 阶段 | 内容 | 完成判据 |
| --- | --- | --- |
| 清理 | 删除调用图、选择性解释、启动门控等旧实现及其测试、工具和文档 | 已完成，旧代码见备份标签 |
| Phase 1 | 可调试前提检查；标准 `RedefineClasses` 路由到 `StructurallyRedefineClasses` | Android 16 上，独立 Agent 经标准入口新增字段和方法成功；仅方法体变化的行为不变 |
| Phase 2 | 上节的校验规则 | 修饰符变化用例提交成功并体现新语义；拒绝用例返回约定错误码且旧定义完整；无效批次整体不提交 |
| Phase 3 | 自有映射与提交：成员删除、类型和签名变化、墓碑成员、JNI ID 解码检查 | 删除和类型变化用例通过；退役引用在反射、JNI、MethodHandle、VarHandle 上抛出约定异常 |
| Phase 4 | 继承关系和类修饰符变化，收窄前的帧检查 | 父类和接口的扩展、移除、恢复用例通过；持有受影响实例的不可去优化帧导致拒绝且旧定义完整 |
| Phase 5 | Android 11、12、13、14、15、17 逐台回归 | 每台设备的结果和 APK 哈希记录在 [VERIFICATION.md](VERIFICATION.md) |

`DcevmRuntime.initialize()` 在 Hook 全部安装成功后返回成功；尚未支持的变化以 JVMTI 错误拒绝。`capabilities()` 逐项报告已支持的变化类别，`fullDcevmComplete` 在 Phase 4 通过设备验收前保持 false。

## 风险

- **Mainline 更新**：ART APEX 会随 Play 系统更新替换。只在 `ClassRedefinition` 前缀等已有固定声明之上读写字段，被 Hook 函数的其他参数原样传回 ART；每次加入新的设备库都运行符号核对工具。
- **ART 服务语义漂移**：自有 `UpdateClassStructurally` 必须逐版本对照 AOSP 源码的调用顺序；某版本的服务签名不同时，在 `platform/abi/arm64/androidNN/` 增加声明，不复制整套实现。
- **墓碑成员的边界**：墓碑方法和字段的形状必须保证任何调用路径都只会抛出异常，不会按错误布局读写对象。
