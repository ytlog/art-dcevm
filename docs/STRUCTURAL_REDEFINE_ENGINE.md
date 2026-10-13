# 基于 ART 结构重定义事务的类演进引擎

状态：Phase 1–2 已实现，并在 2026-10-12 的一台 Android 16 / API 36、arm64 设备上通过指定的 12 次标准 JVMTI 提交与行为检查，见 [VERIFICATION.md](VERIFICATION.md)。Phase 3（方案 C 加字段退役）已实现，固定声明由运行时探测选择，2026-10-13 在同一台 Android 16 设备上通过 23 个场景、799 项检查（含 CheckJNI 开启及线程强制解释执行），见 [VERIFICATION.md](VERIFICATION.md)；Phase 4 与其他版本设备回归尚待实施。本文取代"调用图选择性解释执行"路线；该路线的代码已备份在本地分支 `backup/call-graph-engine-20261012` 和标签 `backup-call-graph-engine-20261012`。

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

### 2026-10-13 Phase 3/4 源码与逐库核对

**结论：核心提交服务有命名符号支持，但目前不能证明自有提交能在全部 27 套库上仅凭现有固定声明正确运行，也不能按原“墓碑映射加两个 JNI 解码 Hook”方案承诺完整 Phase 3。** 尚有 vivo 锁 ABI、源码内联/模板声明、跨运行时容器 ABI 和退役访问语义缺口。Phase 4 还需处理父类/接口解析、批次拓扑与分配声明。该次调查只扩充符号目录与核对工具，未修改引擎；后续方法先行实现采用下文方案 C，仍不扩展为全套自有提交。

核对范围为四个函数的直接操作、字段复制/清空与排序等局部助手、JNI 解码接管点，以及明确列出的替代服务；已复用的 ART 服务内部调用和可由增强库诊断替换的日志/格式化函数不递归展开。完整服务、精确修饰名、源码分组与位置见 [tools/structural-symbol-audit.json](../tools/structural-symbol-audit.json)。其中 `names: []` 表示源码内联、模板或固定布局操作无需独立名称，不能解读为整个 ELF 中不存在任何偶然生成的模板实例。

源码使用 AOSP 标签 `android-11.0.0_r1`、`android-13.0.0_r1`、`android-15.0.0_r1`；Android 15 对应提交 `bebbc3cc49f2d9d5420197df0a336fbc3fcbea40`，`android16-release` 固定为 `ed6c006bd06ae060bd9698fd2cb25c4865512ec3`。审计清单记录所读文件 SHA-256，避免可移动分支改变复核输入。

| 函数（`openjdkjvmti/ti_redefine.cc`） | Android 11 | Android 13 | Android 15 | Android 16 固定提交 |
| --- | --- | --- | --- | --- |
| `CollectNewFieldAndMethodMappings` | [2612](https://android.googlesource.com/platform/art/+/android-11.0.0_r1/openjdkjvmti/ti_redefine.cc#2612) | [2578](https://android.googlesource.com/platform/art/+/android-13.0.0_r1/openjdkjvmti/ti_redefine.cc#2578) | [2607](https://android.googlesource.com/platform/art/+/bebbc3cc49f2d9d5420197df0a336fbc3fcbea40/openjdkjvmti/ti_redefine.cc#2607) | [2602](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/openjdkjvmti/ti_redefine.cc#2602) |
| `UpdateClassStructurally` | [2755](https://android.googlesource.com/platform/art/+/android-11.0.0_r1/openjdkjvmti/ti_redefine.cc#2755) | [2721](https://android.googlesource.com/platform/art/+/android-13.0.0_r1/openjdkjvmti/ti_redefine.cc#2721) | [2750](https://android.googlesource.com/platform/art/+/bebbc3cc49f2d9d5420197df0a336fbc3fcbea40/openjdkjvmti/ti_redefine.cc#2750) | [2748](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/openjdkjvmti/ti_redefine.cc#2748) |
| `FinishNewClassAllocations` | [1852](https://android.googlesource.com/platform/art/+/android-11.0.0_r1/openjdkjvmti/ti_redefine.cc#1852) | [1847](https://android.googlesource.com/platform/art/+/android-13.0.0_r1/openjdkjvmti/ti_redefine.cc#1847) | [1867](https://android.googlesource.com/platform/art/+/bebbc3cc49f2d9d5420197df0a336fbc3fcbea40/openjdkjvmti/ti_redefine.cc#1867) | [1865](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/openjdkjvmti/ti_redefine.cc#1865) |
| `AllocateNewClassObject`（完整参数） | [2042](https://android.googlesource.com/platform/art/+/android-11.0.0_r1/openjdkjvmti/ti_redefine.cc#2042) | [2032](https://android.googlesource.com/platform/art/+/android-13.0.0_r1/openjdkjvmti/ti_redefine.cc#2032) | [2052](https://android.googlesource.com/platform/art/+/bebbc3cc49f2d9d5420197df0a336fbc3fcbea40/openjdkjvmti/ti_redefine.cc#2052) | [2051](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/openjdkjvmti/ti_redefine.cc#2051) |

### 所需服务及源码位置

下表中的声明路径均相对 AOSP ART；四版调用位置与声明锚点逐项保存在审计清单的 `sourceGroups`，可结合上述函数链接复核。`D27` 表示精确名称在 27 套 `.dynsym` 中全部定义；只统计定义，未定义导入不算。内联/模板中的读取、标志设置、GC 屏障仍需要固定源码声明，不能用任意字段偏移或裸 `memcpy` 替代。

| 服务/操作 | 声明或实现源码 | 命名证据与实现边界 |
| --- | --- | --- |
| 四个接管函数；`CollectAndCreateNewInstances` | `openjdkjvmti/ti_redefine.cc` / `.h` | D27；实例分配继续由 ART 执行 |
| `Runtime::VisitReflectiveTargets(ReflectiveValueVisitor*)` | `runtime/runtime.h`、`runtime/reflective_value_visitor.h` | D27；双 lambda 重载为模板，visitor 虚函数顺序与 `ReflectionSourceInfo` 必须固定声明 |
| `HeapExtensions::ReplaceReferences` | `openjdkjvmti/ti_heap.h` / `.cc` | D27；类、实例、堆与根替换继续调用 ART；需要 `ObjPtr`、`HashObjPtr` 和 `std::__1::unordered_map` ABI |
| `Instrumentation::DeoptimizeAllThreadFrames` | `runtime/instrumentation.h` / `.cc` | D27；继续复用 ART 的帧去优化 |
| `JitCodeCache::InvalidateAllCompiledCode` | `runtime/jit/jit_code_cache.h` / `.cc` | D27；仅在 `GetJit()` 非空时调用，Jit owner/getter 为内联 |
| `ThreadList::ForEach(callback, void*)`；`InterpreterCache::Clear(Thread*)` | `runtime/thread_list.h` / `.cc`、`runtime/interpreter/interpreter_cache.h` / `.cc` | 两者 D27；lambda `ForEach` 为模板；`Thread::GetInterpreterCache` 为内联 |
| `Locks::thread_list_lock_`；`Mutex::ExclusiveLock/ExclusiveUnlock` | `runtime/base/mutex.h` / `.cc` | 锁变量和 Unlock D27；标准 Lock 仅 D26，vivo 另有签名，见下文 |
| `SetEntryPointsForObsoleteMethod`；`ClassExt::SetObsoleteClass` | `runtime/class_linker.h` / `.cc`、`runtime/mirror/class_ext.h` / `.cc` | 两者 D27；原 native quick 入口不能自行改写（栈帧大小/栈遍历依赖它） |
| 类 obsolete 标记；方法 obsolete/dont-compile 标记 | `runtime/mirror/class.h`、`runtime/art_method.h` | `SetObsoleteObject`、`SetIsObsolete`、`SetDontCompile` 为内联，无可依赖的独立符号 |
| native JNI 入口复制；旧成员 declaring class 恢复 | `runtime/art_method.h`、`runtime/art_field-inl.h` | JNI 入口 Get/Set、`SetDeclaringClass` 为内联；只复制兼容且仍存活的 native 方法 |
| `Class::SetStatusLocked`、`SetStatus` | `runtime/mirror/class.h` / `.cc` | D27；状态谓词与读取为内联 |
| 字段身份；方法身份、同名同签名判断 | `runtime/art_field-inl.h`、`runtime/art_method-inl.h` / `.h`、DEX 头声明 | 字段名字/类型及 `GetSignature` 等内联，偶然发射符号覆盖不全；`HasSameNameAndSignature` D27；`Signature::operator==` 在 libart 为 S17/G10 |
| `FindDeclaredStaticField/InstanceField`；`FindClassMethod` | `runtime/mirror/class.h` / `.cc` | 所需 `string_view,string_view` 字段重载及 `string_view,Signature,PointerSize` 方法重载 D27；其他重载是不同服务，不能当成版本替换 |
| 八种 primitive 字段 Get 与 `Set*<false>`，引用 Get/Set | `runtime/art_field-inl.h` | Get 为内联，Set 为模板；primitive 无观察到的独立名称，引用 Get/Set 仅 S2；必须保留 volatile 与 GC 读写屏障语义 |
| `CopyField/CopyFields/ClearField/ClearFields/CopyAndClearFields` | `ti_redefine.cc` 中局部 static 助手（11:2641–2753；13:2607–2719；15:2636–2748；16:2630–2746） | `CopyFields` G27，`ClearFields` G9，`CopyAndClearFields` G18；其他已内联。保留函数体不证明所有调用点经过它 |
| 锁字迁移/清零、每字段写屏障 | `runtime/mirror/object-inl.h`、`runtime/lock_word.h`、`runtime/write_barrier.h` | `GetLockWord/SetLockWord`、`LockWord::Default`、`ForEveryFieldWrite` 为内联/模板，需源码 Object/锁字/GC 声明 |
| 成员范围、对象数组 Get/Set/Iterate/GetLength、对象 GetClass | `runtime/mirror/class-inl.h`、`object_array-inl.h`、`array.h`、`object-inl.h` | 内联/模板；11/13/15 的 I/S 字段数组在 16 合并为 `GetFields`，需源代码布局选择 |
| Runtime/Thread/Jit getters；无挂起作用域 | `runtime/runtime.h`、`thread.h` / `thread-inl.h`、`jit/jit.h` | 内联；`Runtime::instance_` D27 只解决获取 owner，不能代替完整 getter 布局声明 |
| Holder/Iter、Handle/MutableHandle/ObjPtr、范围/数组模板 | `ti_redefine.cc` 内部 Holder/Iter 定义、`runtime/handle_scope*.h`、`handle.h`、`obj_ptr*.h` | 多为内联；Holder/Iter 不在公共 `ti_redefine.h`；当前三个前缀声明不足 |
| resolved 子类枚举、拓扑比较 | `runtime/gc/heap.h`、`ti_redefine.cc` 的 `CompareClasses` | `Heap::VisitObjects` 为 ALWAYS_INLINE 模板；`CompareClasses` D27。`VisitClasses` 虽 D27，仅访问类表，不能据此证明与堆枚举集合等价 |
| Class 大小计算、ClassAccessor/DEX 访问 | `ti_redefine.cc` 的 `GetNewClassSize`、`runtime/mirror/class-inl.h`、`libdexfile/dex/class_accessor*.h` / `dex_file*.h`（11 路径为 `dex/…`） | `GetNewClassSize` D27；内联 `ComputeClassSize` 仅局部保留且参数变化，应复用前者；DEX 包装与分类访问为内联/模板 |
| `AllocClass`、`SetDexCache`、`SetupClass`、`LoadClass`、`LinkClass` | `runtime/class_linker.h` / `.cc`、`runtime/mirror/class.h` / `.cc` | D27；类布局/vtable/iftable 继续让 ClassLinker 链接 |
| `ObjectLock<Class>`；MonitorEnter/Exit | `runtime/object_lock.h`、`runtime/monitor.h` / `.cc` | ObjectLock C1/C2/D1/D2 和两个 Monitor 服务 D27，可复用命名 RAII 入口；ObjectLock 对象 ABI仍要声明 |
| 父类/接口、iftable、clinit 线程、hidden-api 与 DEX/cache 元数据 | `runtime/mirror/class*.h`、`dex_cache*.h` | 源码 getter/setter 内联；11 还复制 verification-attempted 标记和写新类 resolved type，13 起删去这两步 |
| `ForceClassInitialized`；三种 `Ensure*Ids` | `runtime/class_linker.h` / `.cc`、`runtime/mirror/class.h` / `.cc` | D27；JNI ID 数组必须在不可失败提交阶段之前分配 |
| 新方法代码初始化；`SetNotIntrinsic` | `runtime/class_linker.cc`、`runtime/instrumentation.h` / `.cc`、`runtime/art_method.cc` | 初始化按库选择 E/I2/R，见逐库表；`SetNotIntrinsic` D27 |
| `ObjectArray<Class>::Alloc`、`Array::Alloc`；变长 handle scope | `runtime/mirror/object_array-alloc-inl.h`、`array-alloc-inl.h`、`runtime/handle_scope-inl.h` | 内联模板；ObjectArray 实例仅 S/G20，Array 无独立名称；scope 析构 S/G16、NewHandle S/G18；优先保留原 ART 分配辅助路径 |
| 失败记录、OOM 断言、清除 pending exception | `openjdkjvmti/ti_redefine.h` / `.cc`、`runtime/thread.h` / `.cc` | `Redefiner::RecordFailure`、`Thread::AssertPendingOOMException` D27；ClassRedefinition 转发器仅 G23，`ClearException` 内联。自行记录诊断可沿用当前写 result 的做法 |
| `JniIdManager::DecodeFieldId/DecodeMethodId` | `runtime/jni/jni_id_manager.h` / `.cc` | D27；存在解码旁路及调用方直接解引用问题，见可行性结论 |
| 新层级解析的候选 `LoadSuperAndInterfaces`、`DoResolveType` | `runtime/class_linker.h` / `.cc`、`class_linker-inl.h` | 前者 D19、缺8；handles 版 `DoResolveType` D27，须配合固定源码缓存/校验策略，不能只替换 superclass |

### 逐套符号表结果

该次调查的公共目录含 **49 个精确名称**：47 个要求 `.dynsym`，两个允许命名局部表；27/27 套全部通过。保留原 17 个名称，新增 31 个全套 `.dynsym` 服务及 `CopyFields` 候选。标准 JVMTI 表使用的 `JvmtiFunctions::RedefineClasses` 包装器和 `CopyFields` 均为 G27；前者的调用覆盖已经有 Phase 1–2 设备证据，后者尚未安装或验证。其余公共目录服务在 API 30–37 的每一套均为 D。**未加入**只覆盖部分库的初始化、Mutex Lock、`LoadSuperAndInterfaces`、`ComputeClassSize` 和偶然生成的字段访问器。

下表按此次排序给出匿名集合 ID；集合 ID 区分采集集合，库哈希前缀区分实际内容；不同集合可以包含相同库字节。D＝`.dynsym` 中定义（可能也有 `.symtab` 副本）；S＝仅 `.symtab`；G＝仅 `.gnu_debugdata`；`—`＝该精确名称在三表均缺失。E＝`SetEntryPointsToInterpreter`，I2＝双参数 `InitializeMethodsCode`，R＝`ReinitializeMethodsCode`；C6/C7＝`ComputeClassSize` 的六/七个 uint32 参数变体。ObjectArray Alloc 列依次是无显式 allocator 与带 allocator 两个模板重载。精确修饰名与逐名称所有表位置保存于完整 JSON 报告，未去掉编译器后缀，也未把同名导入当作定义。

| 集合 / API / OEM / 型号（libart SHA-256 前缀） | 初始化 | Super/Interfaces | 同步缓存包装 | ClassSize | ObjectArray Alloc | Field GetName | Mutex Lock |
| --- | --- | --- | --- | --- | --- | --- | --- |
| set-01 / 30 / google / pixel-5-redfin (`dafbaad94864`) | ED | D | — | — | —/S | — | D |
| set-02 / 30 / huawei / nco-al00-hwnco-q (`e105ff0ddd15`) | ED | D | — | — | —/S | — | D |
| set-03 / 30 / oneplus / gm1910-oneplus7pro (`75914dc68904`) | ED | D | — | — | —/S | — | D |
| set-04 / 30 / xiaomi / 2109119bc-mona (`4e3366076077`) | ED | D | — | — | —/S | — | D |
| set-05 / 31 / google / pixel-6-oriole (`5f7da6b1d7db`) | ED | D | — | — | —/— | — | D |
| set-06 / 31 / honor / lsa-an00-hnlsa (`ecf9776bab1e`) | ED | D | — | — | —/— | — | D |
| set-07 / 31 / oneplus / le2100-oneplus9r (`d7eb3e36884b`) | ED | D | — | C6S | S/— | S | D |
| set-08 / 31 / vivo / v2170a-pd2170 (`69ef38a519ee`) | ED | D | — | — | —/— | — | D+uint |
| set-09 / 32 / google / pixel-6a-bluejay (`d7eb3e36884b`) | ED | D | — | C6S | S/— | S | D |
| set-10 / 33 / google / pixel-7-panther (`97ded1890d0c`) | I2D | D | — | — | S/— | S | D |
| set-11 / 33 / samsung / sm-s908u1-b0q (`3afaae41551e`) | I2D | D | — | — | S/— | S | D |
| set-12 / 33 / xiaomi / m2007j1sc-cas (`c855cb26d909`) | I2D | D | — | — | —/— | S | D |
| set-13 / 33 / xiaomi / m2007j1sc-cas (`c855cb26d909`) | I2D | D | — | — | —/— | S | D |
| set-14 / 34 / google / pixel-8-shiba (`6d8ade7ed305`) | I2D | D | — | — | S/— | S | D |
| set-15 / 34 / google / pixel-9-tokay (`4166f04af3a7`) | RD | — | D | — | G/— | G | D |
| set-16 / 34 / google / pixel-9-tokay (`c6652beac3be`) | RD | — | D | — | G/— | G | D |
| set-17 / 34 / samsung / sm-s9210-e1q (`5555b485efbd`) | I2D | D | — | — | S/— | S | D |
| set-18 / 35 / google / pixel-8-pro-husky (`323fa0338f73`) | I2D | D | — | C6G | G/— | G | D |
| set-19 / 35 / vivo / v2419a-pd2419 (`683b4c1e133f`) | I2D | D | — | — | —/— | S | D |
| set-20 / 35 / xiaomi / 24129pn74c-dada (`9628c5e83669`) | I2D | D | — | — | —/— | G | D |
| set-21 / 36 / google / pixel-10-frankel (`c96bb3f74501`) | RD | — | G | — | G/— | G | D |
| set-22 / 36 / honor / ptp-an00-hnplp (`c5796d94c738`) | I2D/RD | D | S | C7S | S/— | S | D |
| set-23 / 36 / samsung / sm-s9310-pa1q (`8d3e7b2b9647`) | RD | — | G | — | G/— | G | D |
| set-24 / 36 / xiaomi / 24091rpadc-muyu (`e6e5be907779`) | RD | — | G | — | G/— | G | D |
| set-25 / 36 / xiaomi / 25098pn5ac-pandora (`d2d62445d52f`) | RD | — | G | — | G/— | G | D |
| set-26 / 37 / google / pixel-10-frankel (`99b2e6f114b4`) | RD | — | D | — | G/— | G | D |
| set-27 / 37 / google / pixel-11-cubs (`d3f6c05fa9e2`) | RD | — | D | — | G/— | G | D |

其他局部名字的完整逐套结果仍在报告：字段 `GetName` S10/G10（缺7），`GetNameView` 仅 set-18 的 G；引用字段 GetObject/SetObject<false> 仅 set-07/09 的 S；八种 primitive Get/Set、obsolete 标志与 JNI 入口/declaring-class setters 没有可依赖的独立服务。`Signature::operator==` 和 `Heap::AllocObjectWithAllocator<true,true,SetLengthVisitor>` 在全部 libart 中 S17/G10。后者可作为固定源码 Array 分配包装的候选，但其 GC/visitor/allocator ABI 尚未审核，未加入可部署目录，不能由名称存在推出可调用。

精确名称变化与替代：

- E 为 `_ZNK3art11ClassLinker27SetEntryPointsToInterpreterEPNS_9ArtMethodE`（D9，API 30–32）。I2 为 `_ZN3art15instrumentation15Instrumentation21InitializeMethodsCodeEPNS_9ArtMethodEPKv`（D10）。R 为 `_ZN3art15instrumentation15Instrumentation23ReinitializeMethodsCodeEPNS_9ArtMethodE`（D9，含较旧系统 API 上的新 ART）。set-22 同时有 I2 和 R；不能只凭 API 选声明。16 源码的三参数 Initialize（末尾 `NS_11PointerSizeE`）在这 27 套三表中均缺失，直接调用 R 更贴合该版源码。各初始化服务负责的 instrumentation/clinit/native 语义不同，不能随意互换。
- C6 为 `_ZN3art6mirror5Class16ComputeClassSizeEbjjjjjjNS_11PointerSizeE`（S2/G1）；C7 多一个 `j`（S1）。调用全套 D27 的 `ClassRedefinition::GetNewClassSize(ClassAccessor&)` 保留 ART 大小计算，并声明匹配的 ClassAccessor/DEX ABI。
- 标准锁为 `_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadE`（D26）；set-08 仅有尾部增加 `j` 的变体（D1）。额外 uint 参数没有被核对的 AOSP 固定声明：[Android 12 mutex.h:180](https://android.googlesource.com/platform/art/+/android-12.0.0_r1/runtime/base/mutex.h#180) 仍只有 `Thread*`。不得猜参数为零；需对应 OEM 源码声明，或通过已验证的 ART 原生同步包装改变接管边界。未证明重试 TryLock 等价。
- `_ZNK3art10ThreadList22ClearInterpreterCachesEv` 为 D4/S1/G4，符合独占 mutator 锁提交语义但缺18。`Thread::ClearAllInterpreterCaches` 虽 D27，[其实现](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/thread.cc#4879) 使用 RunCheckpoint；[后者断言不持有 exclusive mutator lock](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/thread_list.cc#307)，且运行线程回调可能未完成，不能放进当前 Update 提交段。标准路径仍为正确加锁的 ForEach + Clear + 固定 Thread 缓存声明，vivo 的锁声明缺口尚待解决。
- `_ZN3art11ClassLinker22LoadSuperAndInterfacesENS_6HandleINS_6mirror5ClassEEERKNS_7DexFileE` D19，set-15/16/21/23–27 缺失。handles 版 `_ZN3art11ClassLinker13DoResolveTypeENS_3dex9TypeIndexENS_6HandleINS_6mirror8DexCacheEEENS3_INS4_11ClassLoaderEEE` D27；ResolveType 的内联包装偶然发射不全，应按源码组合 DEX cache 查找与该命名慢路径。

### 对 Phase 3/4 方案的约束

**自有提交的执行步骤有主要命名服务，但“在所有样本上完整可行”还不成立。** 27 套库存只证明名称，不证明固定声明、返回类型、函数对象或容器 ABI。调查时 `platform/abi/arm64/common/redefinition.h` 只含三个前缀；若自行提交，至少需要补齐源代码对应的 Redefiner runtime/self、Holder/Iter 槽与标志、ObjPtr/Handle、Class/ObjectArray、ArtField/ArtMethod、reflective visitor、Runtime/Jit owner、Thread cache、ObjectLock/Mutex、锁字/GC 屏障，以及 ART `std::__1::map/unordered_map` 与 NDK `std::__ndk1` 的跨边界兼容声明/容器策略。共同内容放 common，确有差异才放 androidNN；ART Mainline 与 OEM 构建需按实际库证据选择，不能用 API 对象偏移猜测。

必须保留 [ART Run 的最终事务](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/openjdkjvmti/ti_redefine.cc#2499)：从“不可失败点”起不再分配 Java 对象或调用可能挂起的 JNI/JVMTI；所有新增目标、根与校验提前完成。原 ART 提供 GC 停止、暂停线程、实例创建与回滚前置步骤；自有逻辑只改兼容成员匹配与字段拷贝决策，按源码顺序调用 ART 引用改写、去优化、obsolete 和失效服务。引用替换前保存旧 Class 集合，替换后按该版源码修复旧成员 declaring class、保活旧 Class，再失效 JIT 和同步清缓存。11 与 13+ 的 shadowing/DexCache 判定优先级不同，旧 native 入口复制还假设新方法同签名且仍是 native，删除/签名变化必须改正该决策。

有一个更窄的备选：`CopyFields` 的完整命名符号为 G27，可以调查只接管字段匹配/复制而保留原 `UpdateClassStructurally` 的锁字、清空、屏障和全套提交。源码的递归体存在**不证明首次字段拷贝没有内联**；需逐版本设备证明静态、当前类和祖先字段拷贝均经过 Hook，以及映射 Hook 覆盖，才能采用。它也不解决原 native 复制假设或退役访问语义，因此此次只加入候选声明。

**完整 Phase 3 必须先调整退役访问方案（以下为当时的调查结论，最终方案见“字段退役”一节）。** [Field 的 reflective visitor](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/mirror/field.cc#28) 要求目标属于声明类的真实字段数组，并复制 offset；[反射 Field.get](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/native/java_lang_reflect_Field.cc#78)、[MethodHandle 字段访问](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/method_handles.cc#621)、[VarHandle 字段访问](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/mirror/var_handle.cc#1661) 直接读写字段 offset。ArtField 没有能抛异常的方法入口；有效墓碑字段仍会读写，无效字段或 null 会崩溃，不能自然产生 NoSuchFieldError。需要重新核对这些访问边界可用的命名守卫/语义，或继续拒绝字段退役，不能宣称一个映射覆盖全部异常行为。

同样，[JNI 内联解码](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/jni/jni_internal.h#84) 对 pointer ID 直接 cast，索引 ID 才经过 manager；即使 manager 被 Hook，[调用方](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/jni/jni_internal.cc#1552) 解码后立即解引用，不检查 null 或 pending exception。在 decoder 抛异常后返回 null 不安全，返回普通墓碑仍会读写。仅要求启动时 debuggable 也不能作为这些路径已覆盖的证明。方法墓碑还需真实链接成员、正确分派和抛异常入口的运行证据。

**Phase 4 可继续命名服务路线，但必须扩展解析策略。** 原 allocator 明言层级未变化；完整新层级需要 [LoadSuperAndInterfaces](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/class_linker.cc#6665) 的自循环、可访问性、父类/接口解析语义。缺独立符号时，可研究固定源码包装 DoResolveType 和新 DEX resolved cache；外部类型在暂停类加载前解析，批次父类/接口按新拓扑映射，保持 LinkClass 生成 vtable/iftable。LinkClass 的父类检查不能代替全部接口可访问性校验。不要直接改 super 后假定接口已加载，也不要调用会公开新类的 DefineClass 代替事务内 stripped allocation。自有 Finish 的 ObjectArray/handle 模板可声明源码包装调用 ART 现存分配服务，或保留原 Finish/Allocate 辅助路径，不重新实现分配器。

### Phase 3 方案 C：方法先行，保留原生提交

方案 C 支持普通类中 concrete Java 方法删除和签名替换；字段守卫可用时还支持字段删除、类型变化与 static/实例互换（见“字段退役”），否则这些变化以 `SCHEMA_CHANGED`（64）拒绝。native、abstract、接口声明、构造器与类初始化器的退役暂以 `METHOD_DELETED`（67）拒绝。层级与类修饰符变化继续沿用 Phase 2 的拒绝码。

实现保留 ART `FinishNewClassAllocations`、`AllocateNewClassObject`、`CollectAndCreateNewInstances` 和 `UpdateClassStructurally`，不接管字段复制、锁字、实例迁移、引用遍历、去优化或缓存失效。`Update` Hook 只设置 visitor 的事务作用域并调用原函数。探测通过的事务一律使用自有映射，以处理没有声明退役、但子类 copied default/miranda 消失的情况；映射在 ART 仍可失败的 `CollectAndCreateNewInstances` 之后预先算好，提交时只写入 ART 的 map。探测未通过的事务使用 ART 原映射，策略已拒绝它无法处理的变化。最终 DEX 策略生成退役声明清单；映射以真实 declaring class 与同名同签名成员区分声明、继承及 copied 成员，被删除的真实旧声明映射到自身并退役，不制造假的成员或偏移。

原 ART 对所有旧方法的 obsolete 标记、旧 declaring class 修复和 ClassExt 保活照常进行。visitor 代理先清除退役方法的 DexCache resolved-method 引用，让普通调用重新解析；删除覆写方法后可落到父类或接口默认实现。反射、JNI 与 MethodHandle 保留真实旧成员目标，并在新的访问/分派之前抛 `NoSuchMethodError`。缓存的反射 `Method.invoke` 按调用约定包装为 `InvocationTargetException`；恢复同名同签名方法会产生新成员，旧缓存目标仍退役。签名替换是旧签名退役与新签名新增。

新调用的守卫只放在退役方法仍可能被直接拿到的入口，即缓存的反射对象、JNI ID 和 MethodHandle。普通字节码调用无需守卫：ART 在提交时访问堆中全部 DexCache（含旧类保留的），visitor 代理清除指向退役方法的解析项，重新解析找不到成员就抛 `NoSuchMethodError`；vtable/iftable 只含新方法，线程解释器缓存与 JIT 代码由 ART 清空。设备上逐个去掉 `DoCall<false/true>`、`ArtMethod::Invoke`、`EnterInterpreterFromInvoke` 与 obsolete quick 入口守卫后，包括线程强制解释执行在内的全部用例仍然通过，因此这些守卫已删除。保留的守卫：

- **JNI**：用公开的 JVMTI `SetJNIFunctionTable` 在当前函数表前包装全部 `Call*Method*`（普通、非虚、静态，含 A/V）。退役 ID 只做查表、不解码，直接抛 `NoSuchMethodError`；其余调用转给原表，CheckJNI 开启时原表就是检查表，校验照常。ART 设置覆盖表时会关闭本进程的 Core Platform API 检查（`JNIEnvExt::SetTableOverride`），它只约束平台模块调用方。两个 `EncodeMethodId` 重载记录退役后新编码的索引 ID。
- **反射**：Hook `Method.invoke` 的 native 实现 `Method_invoke`，在 ART 以旧声明类核对接收者之前，用 JNI `FromReflectedMethod` 识别退役目标，按调用约定抛出包装 `NoSuchMethodError` 的 `InvocationTargetException`。
- **MethodHandle**：`MethodHandleInvoke`/`MethodHandleInvokeExact` 在分派前读取句柄的 `artFieldOrMethod`。组合句柄在 Android 11/12 经 invoke-polymorphic 回到这两个入口，13 起经 `MethodHandleInvokeExactWithFrame` 调用 `MethodHandleInvokeExact`；该函数存在时也加守卫，以防编译器内联。

已有帧的去优化恢复/Execute 路径不接管，允许活动旧帧完成旧方法体。

固定声明不按构建编号放行，而由运行时探测选择：

- **声明来源**：逐版核对 `android-11.0.0_r1` 至 `android-17.0.0_r1`（12 含 `12.1.0_r1`）的 AOSP 源码，标签提交记录在 VERIFICATION.md。一致的声明放 `platform/abi/arm64/common/`：Object/ObjectArray、DexCache 前缀、ArtField、12–17 的 ArtMethod、Handle、RedefinitionDataHolder 槽位、反射 visitor 与来源类型、libc++ 指针 map。差异放各版本目录：`android11`–`android17/class.h` 的 Class 字段数组（11–15 为 `ifields_/sfields_`，16–17 为 `fields_`，`access_flags_` 随之移动），以及 `android11/art_method.h`（多 `dex_code_item_offset_`）。
- **安装**：方法退役所需名称（`platform/symbols/method_retirement_symbols.h` 中的非全套名称与公共目录服务）全部解析、NDK map 自检通过才安装 Hook；否则保持 Phase 2，诊断写明缺失项。
- **CheckClass 探测**（ART 仍可失败）：对每个目标，用 `GetMirrorClass` 取 Class，按候选声明核对 `access_flags_` 与 ClassDef 标志。用 `FindDeclared*Field` 返回的 ArtField 先确认各字段数组的位置，再读取数组，逐项核对声明类、JVMTI 修饰符与 DEX 字段身份；按候选 ArtMethod 步长核对声明方法数、修饰符与 DEX 方法身份。确认位置前不解引用候选数组。候选只取不早于平台版本的版本（ART 模块不会比平台旧），与平台同名的版本优先。事务内所有目标都接受的候选才可用；没有可用候选时，该事务按 Phase 2 拒绝退役与拷贝成员变化。
- **CollectAndCreateNewInstances 探测与计划**（ART 最后可失败的一步）：核对槽位中的旧类即目标、替换类使用传入 DEX，再用选中的布局为全部受影响类预先算出字段与方法映射（含 DEX 身份读取）。任何一步失败且该事务依赖自有映射时返回错误，整批不提交；不依赖时退回 ART 原映射。
- **提交期**：只把计划写入 ART 的 map，写入前确认它们是空的 libc++ 树。首次退役后确认 ART 已给退役方法设置 obsolete 标志，否则守卫改为直接查表。MethodHandle 的 `artFieldOrMethod` 偏移通过全套 D27 的 `Class::FindInstanceField` 从 ART 字段表取得，不写死布局。
- **DEX 身份**：通用有界读取同时接受标准 DEX 与 Android 11–15 的 CompactDex（ID 表在主区，字符串与类型列表在独立数据区）；重定义输入仍只接受单类标准 DEX。

共享控制、探测与候选表在 `platform/common/method_retirement.{h,cc}`。目前只有 Android 16 设备证据；其他版本的声明已按源码补齐，由探测决定是否启用，仍须逐台设备回归（Phase 5）。

公共命名目录现为 **57 个名称**（53 个限定 `.dynsym`、4 个允许任一命名表），在 27/27 套库全部通过。退役使用的 ThrowNewException、EncodeMethodId 两重载、GetCanonicalMethod、GetMirrorClass、HasSameNameAndSignature、FindDeclaredStatic/InstanceField 与运行时探测用的 `Class::FindInstanceField` 都为 D27；MethodHandle invoke/invokeExact 为 27/27（D15/S17/G10，同一套库可能同时含多表，不能相加）。删除守卫后不再使用的 ArtMethod Invoke、FromReflectedMethod、ThrowNewWrappedException、obsolete quick/异常投递与 JNI 四个 ID 调用助手已移出目录。这些名字证明可定位；固定声明由运行时探测核对，Hook 覆盖由设备验证确认。

非全套条目的逐库结果如下；名称与完整分表结果保存在审计清单/报告。`Method_invoke` 是局部函数，各构建拼写为无后缀或带一个固定 `.__uniq` 后缀，按两个精确名依次查找，27/27 至少有其一；frame 入口只在 13 起存在。

| 非全套服务 | 任一表存在 / 27 | D / S / G / 缺失 | Android 16 设备库 |
| --- | --- | --- | --- |
| `Method_invoke`（无后缀 / 固定 `.__uniq` 后缀） | 27（11 / 16） | 无后缀 0 / 11 / 0 / 16；带后缀 0 / 6 / 10 / 11 | 带后缀名 |
| MethodHandleInvokeExactWithFrame（可选） | 18 | 6 / 8 / 10 / 9 | G |

ART/NDK 的指针 map 边界限定 libc++ ABI v1 specialization；固定节点布局对照平台 libc++ 源码，编译时检查 NDK 类型大小，初始化时用增强库自己的 empty/one-node map 验证 sentinel、root、size 和 value 布局。这个检查没有读写 ART 私有对象以猜偏移；提交时另核对 ART 传入的 map 为空树。真实 ART map 遍历、替换与销毁已在 Android 16 设备的两轮方法退役/恢复用例中通过；其他构建仍须独立验证。

为避免 GC、class-loader 卸载或 native arena 地址复用让原生退役指针变成悬空键，Native 状态提前创建受影响类的 JNI global roots。失败或没有退役的批次释放新根；成功退役后保留、跨批次去重，ART 引用替换会更新根到新 Class，其 obsolete 链保护旧成员。**这些类及其 class loader 会保留到进程结束**；当前不承诺退役后的类卸载。根去重的 JNI 操作在 registry mutex 之外，最终提交中没有 JNI/JVMTI 分配或状态切换。新加载子类的 copied 成员不会进入永久退役集合；消失的 copied default 指向真正 canonical 成员并清除该次 DexCache 解析，不当作被删除的声明。

能力报告分别给出 `methodDeletion`、`methodSignatureChange`（退役 Hook 已安装），`memberMapping` 报告最近一次探测结果（`unavailable`、`unverified`、`verified: androidNN class layout` 或 `rejected: 原因`）；字段守卫可用时 `fieldDeletion`、`fieldTypeChange` 与泛称 `memberDeletion/memberTypeChange` 为 true；`hierarchyChange`、`fullDcevmComplete` 仍为 false。

复核命令：

```sh
python3 tools/check-structural-symbols.py
python3 tools/check-structural-symbols.py --audit-spec tools/structural-symbol-audit.json --report build/evidence/structural-symbols/phase34-audit.json
python3 tools/test-structural-symbol-audit.py
python3 tools/test-elf-mini-inventory.py
```

完整逐套报告记录每个精确名称的三表位置/缺失、库 SHA-256、审计清单与目录 SHA-256。原始源码、全部名字库存、测试日志与 JSON 报告仅放在已忽略的 `build/evidence/structural-symbols/`，不提交运行库、设备清单或本机配置。公共目录严格检查失败时返回非零；研究候选的缺失被完整报告，不降低目录要求。运行时仍由官方 ShadowHook 按目录限定表范围查找；`.gnu_debugdata` 是命名 ELF 表，未反汇编、读取符号地址、计算对象偏移或证明运行行为。

### 字段退役

旧字段的名称、类型或存储不再出现在新类中时，该旧字段退役：保留真实旧 ArtField，不制造墓碑；新类中同名的新字段从默认值开始。各决策点：

- **提交复制**：ART 的 `CopyFields`/`ClearFields` 按旧类字段数组逐个在新类中查找同名同类型字段，找不到即 `CHECK`。`CollectAndCreateNewInstances` 之后（仍可失败）为每个含退役字段的旧类准备一份去掉退役项的字段数组副本；Update 开始时把旧类的字段数组槽位（16–17 为 `fields_`，11–15 为 `ifields_`/`sfields_`，由探测选中的版本声明给出）换成副本，ART 的 visitor 调用到达时换回原数组（11–17 均在复制之后、visitor 之前），Update 结束时再确认已换回。ART 不读写退役字段，也不需要临时落点；提交后把旧类中退役的静态引用置空，避免永久保留的旧类继续持有对象。
- **字节码**：visitor 清除 DexCache 中的退役字段解析项，`iget/sget` 重新解析时抛 `NoSuchFieldError`；旧活动帧访问退役字段同样报错。
- **JNI**：JNI 函数表包装 `Get/Set<Type>Field`、`GetStatic/SetStatic<Type>Field` 与 `ToReflectedField`。visitor 记录 JNI 表中的退役 ID；`JniIdManager::EncodeFieldId`（新旧两种签名，各两个重载，任一对齐全即可）记录之后编码的退役或占位字段 ID；指针 ID 按地址比对。命中时抛 `NoSuchFieldError`，不解码 ID。
- **反射 `Field`**：`ReplaceReferences` 会把 `Field` 对象的声明类改成新类，而它的下标与 offset 仍描述旧字段，映射回旧字段会在新对象上读写错误位置。visitor 因此把它映射到库内占位类 `com.github.ytlog.artdcevm.internal.RetiredField`（实例）或 `RetiredStaticField`（静态）中同一基本类型的字段，`Field::VisitTarget` 随之改写声明类、下标与 offset。访问实例字段时接收者检查失败，抛 `IllegalArgumentException`；`RetiredStaticField` 的初始化在安装时就已失败，访问静态字段抛 `NoClassDefFoundError`（原因为 `NoSuchFieldError`）。占位 ArtField 在安装时经 JVMTI `GetClassFields`（不初始化类）与上述编码钩子取得；`FromReflectedField` 得到的占位 ID 同样被 JNI 包装拒绝。
- **MethodHandle**：句柄守卫读取 `artFieldOrMethod`，目标是退役字段时抛 `NoSuchFieldError`。
- **VarHandle**：VarHandle 访问器在 11–17 都先检查 `accessModesBitMask`（`VarHandleInvokeAccessorImpl` 与 JIT intrinsic）。visitor 遇到指向退役字段的 VarHandle 时把该掩码清零（偏移经 `Class::FindInstanceField` 取得），之后的访问抛 `UnsupportedOperationException`。
- **无法覆盖**：`sun.misc.Unsafe` 与 `Atomic*FieldUpdater` 等缓存的原始字段 offset 不经过任何 ART 入口。任何改变实例布局的结构重定义（包括 Phase 2 新增字段）都会使它们失效，这是 ART 结构重定义本身的限制。

字段守卫依赖已安装的方法退役 Hook、`EncodeFieldId` 与占位字段；任一缺失时字段变化仍以 `SCHEMA_CHANGED` 拒绝，方法退役不受影响。

## 接管点

下表为当前 Phase 3 方案 C 的行为；Phase 4 决策点仍待实施。

| ART 决策点 | ART 原行为 | 当前增强行为 |
| --- | --- | --- |
| 公开表的 `JvmtiFunctions::RedefineClasses` | 普通重定义拒绝结构变化；导出入口可能内联 | 路由到原始结构事务，保留参数、能力检查、ClassFileLoadHook 和普通方法体更新 |
| `CheckClass` | 类修饰符、父类、接口不得变化 | 继续拒绝这些变化；解析最终 DEX，评估 Phase 2 与方法退役策略 |
| `CheckFields` | 只增不删、修饰符不能变化 | 保留 Phase 2 增加/修饰符支持；字段守卫可用且探测通过时接受删除、改类型或 storage，否则返回 64 |
| `CheckMethods` | 只增不删、修饰符不能变化 | 探测通过的事务允许普通类 concrete Java 方法删除/签名替换；不支持的退役返回 67；结构变化调用 RecordNewMethodAdded |
| `CollectNewFieldAndMethodMappings` | 缺同名同签名方法会 CHECK，包括重链接子类的 copied default/miranda 消失时 | 写入预先算好的映射：消失的声明保留真实旧成员并退役，消失的 copied 方法映射 canonical 并清除解析缓存；没有计划的事务调用原映射 |
| `CollectAndCreateNewInstances` | 为旧实例分配新实例 | 原函数成功后核对槽位声明并预算映射；依赖自有映射的事务在失败时返回错误 |
| `UpdateClassStructurally` | 原生字段/锁字迁移、obsolete、引用替换、去优化、缓存失效 | 只包裹 visitor 作用域，完整调用原提交；有退役字段时在复制阶段换用不含退役项的旧类字段数组；不 Hook CopyFields，不自行提交或重新实现失效 |
| `Runtime::VisitReflectiveTargets` | 原成员映射与 shadowing 处理 | 先换回旧类字段数组；清除退役方法/字段的 DexCache 项，记录退役 JNI ID，把退役字段的反射 `Field` 映射到占位字段并清零其 VarHandle 访问掩码；其余交给原 visitor |
| JNI 函数表、`Method_invoke`、MethodHandle 入口、JNI Encode | 缓存的反射/JNI/句柄目标是旧声明类的 obsolete 方法：反射与 CheckJNI 报接收者类型错误，句柄与 JNI 按旧 vtable 索引可能分派到别的方法 | 仅对显式退役目标在分派前报 NoSuchMethodError，其他调用转发；字节码调用由 DexCache 清除后的重新解析报错；旧活动帧恢复路径保持 ART |
| `AllocateNewClassObject` / `FinishNewClassAllocations` | 使用旧层级重建类和子类 | 方案 C 完全保留；改变新父类/接口的分配策略留待 Phase 4 |

## 语义

- **新增字段**：已有对象取零值或 `null`，不重新执行构造器；新建对象执行新构造器。
- **删除字段、改类型或 static/实例互换**：字段守卫可用且探测通过时，旧字段退役，新字段从零值或 `null` 开始，兼容字段由 ART 拷贝并保留原值。
- **删除方法、改签名（方案 C）**：探测通过的事务中普通类 concrete Java 方法的旧目标退役，新访问抛 `NoSuchMethodError`，反射 invoke 包装异常；普通虚调用重新解析，删除覆写后按新 vtable/iftable 落到父类或默认实现。旧活动帧继续旧方法体；恢复只产生新目标。native、abstract、接口声明、构造器/初始化器退役仍拒绝。
- **退役字段的引用**：字节码、JNI 与 MethodHandle 抛 `NoSuchFieldError`；缓存的反射 `Field` 访问实例字段抛 `IllegalArgumentException`、访问静态字段抛 `NoClassDefFoundError`；VarHandle 抛 `UnsupportedOperationException`。恢复同名字段会产生新字段，旧的缓存引用保持退役。
- **修饰符**：新类直接使用新 DEX 的修饰符。访问收窄后，已解析的旧访问会因 DexCache 被清除而重新做访问检查。
- **继承关系变化（Phase 4 后续目标）**：父类和接口的变化由 ClassLinker 链接新类时处理。收窄（失去某个父类型）前检查：若有不可去优化的帧持有受影响类的实例，返回 `JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED`（66）并保持旧定义，帧退出后可重试。
- **冲突拒绝**：加 final 但已有子类覆写、存活实例将继承未实现的抽象方法等，沿用现有拒绝码，整批不提交。

## 活动帧策略

在启动时即可调试的进程里，应用代码以可调试方式 JIT 编译或解释执行，ART 的 `DeoptimizeAllThreadFrames` 能覆盖全部应用帧。仍然不可去优化的只有 boot 镜像帧；boot 代码是独立编译的，不会内联应用代码，这些帧只**持有引用**。因此：

- 成员变化：不需要额外处理，由 ART 去优化和 obsolete 方法机制保证。
- 继承关系收窄（Phase 4 后续目标，当前仍拒绝）：用 GC 根访问器枚举不可去优化帧持有的引用（栈图为 GC 本来就记录了这些信息），按上节规则拒绝或放行。不解码 CodeInfo、不恢复 vreg。

## 目录落点

遵循现有分层，不新建版本后端。`artdcevm/src/main/cpp` 下：

| 内容 | 位置 |
| --- | --- |
| JVMTI Agent 入口与 `DcevmRuntime` 的 JNI 导出 | `core/agent.cc` |
| 接管点命名符号（11–17 共用）；`Find()` 用官方 ShadowHook（Maven prefab）按目录指定的 ELF 符号表范围查找 | `platform/symbols/structural_redefine_symbols.h`、`art_symbol_compat.h` |
| 11–17 共用的 `ClassRedefinition` / `Redefiner` / `DexFile` 前缀声明 | `platform/abi/arm64/common/redefinition.h`；确有版本差异的声明放 `platform/abi/arm64/androidNN/` |
| 引擎：前提检查、Hook 安装、路由、校验 Hook、能力报告 | `platform/common/structural_redefine_engine.{h,cc}` |
| 校验策略（纯函数，主机可测） | `platform/common/structural_policy.{h,cc}` |
| 方法退役、运行时探测与候选布局表 | `platform/common/method_retirement.{h,cc}`；通用声明在 `platform/abi/arm64/common/`，版本差异在 `platform/abi/arm64/android11`–`android17/`，非全套名称在 `platform/symbols/method_retirement_symbols.h`；退役字段的反射占位类在 `artdcevm/src/main/java/.../internal/RetiredField.java`、`RetiredStaticField.java` |
| 用公开 JVMTI 读取旧类结构 | `platform/common/schema_snapshot.{h,cc}` |
| 读取 ART 已映射 DEX 中的类声明 | `evolution/class_schema.h`、`evolution/dex_schema.cc` |

## Phase 2 校验规则

`CheckClass` 时读取新 DEX（`self->dex_file_`，ClassDef 0）和旧类（公开 JVMTI），交给 `structural_policy` 评估；结果按 `ClassRedefinition*` 缓存在线程局部，供随后的 `CheckFields` / `CheckMethods` 使用。拒绝时只写 `driver_->result_` 并返回 false（`Redefiner::RecordFailure` 本身也只做这件事和格式化消息），原因写入引擎诊断和 logcat。接受且有新增成员或修饰符变化时调用 `RecordNewFieldAdded` / `RecordNewMethodAdded`，让 ART 走结构路径，新类采用新 DEX 的修饰符。

成员身份：字段为（名称、类型、static），方法为（名称、描述符）。比较前把 DEX 原始标志规范化为 JVMTI 视图（与 `ti_method.cc`、`ti_field.cc`、`ti_class.cc` 一致：取 Java 标志位，方法的 declared-synchronized 映射为 synchronized，abstract 方法去掉 native）。

| 变化 | 当前 Phase 2 / Phase 3 方案 C 结果 |
| --- | --- |
| 新增字段、方法 | 接受 |
| 已有成员的访问级别、final、volatile、transient、synchronized 变化；Java → native | 接受 |
| 方法新增 final，且已加载的子类覆写它 | 拒绝，`METHOD_MODIFIERS_CHANGED` |
| 新增或改变访问级别的实例方法，可能顶替已加载子类的 copied default/miranda（子类的接口声明它，子类本身与目标的父类都没有相应 public 实现） | 探测未通过时拒绝：新增为 `METHOD_ADDED`（63），访问级别变化为 `METHOD_MODIFIERS_CHANGED`；探测通过时接受，由自有映射处理 |
| native → Java，abstract 相关变化 | 拒绝，`METHOD_MODIFIERS_CHANGED` |
| 删除字段，字段类型或 static 属性变化 | 字段守卫可用且探测通过时接受（旧字段退役）；否则拒绝 `SCHEMA_CHANGED` |
| 删除方法，方法签名变化 | 方案 C 在探测通过的事务接受普通类 concrete Java 方法；其余拒绝 `METHOD_DELETED` |
| 类修饰符变化 | 拒绝，`CLASS_MODIFIERS_CHANGED`（Phase 4 支持） |
| 父类、接口变化 | 拒绝，`HIERARCHY_CHANGED`（Phase 4 支持） |

## 引擎安装与事务流程（`structural_redefine_engine.cc`）

接口已在 `structural_redefine_engine.h` 定义：`Install(JavaVM*, jvmtiEnv*)` 与 `Capabilities()`，由 `core/agent.cc` 调用。

1. **Install**（幂等，由 agent 的互斥锁串行化）：
   1. 用自有 jvmtiEnv 的潜在能力确认 `can_redefine_classes`，否则失败。
   2. 直接调用官方 `shadowhook_init()` 初始化，再通过 `symbols::Find()` 解析 MVP 用到的 7 个符号：公开表的 `JvmtiFunctions::RedefineClasses`、结构入口、三个校验点、两个 `RecordNew*Added`。任一缺失就失败，并写明缺失的符号；公开表与命名包装器不一致时也失败。
   3. 方案 C 解析映射、探测、visitor 与访问守卫所需名称并做 NDK map 自检；任一缺失则不安装这些 Hook，继续 Phase 2，诊断写明原因。声明是否适用由每次事务的探测决定。
   4. 保存结构入口、校验点与方案 C 调用的原始地址；直接调用官方 `shadowhook_hook_sym_addr()` 替换三个校验点及已启用的方案 C Hook，标准包装器最后安装。失败信息取 `shadowhook_get_errno()` / `shadowhook_to_errmsg()`；任一步失败，直接调用 `shadowhook_unhook()` 逆序撤销已安装的部分，不再另设 Hook API 封装。
2. **标准入口**：
   1. 进入 ART 之前，线程仍处于 Native 状态，此时对每个目标类：用 `live_schema::ReadLiveSchema` 读取旧声明，并用 JVMTI 收集已加载子类声明的方法、子类接口中子类未声明的方法，以及目标父类的 public 方法。
   2. 按 `jclass` 存入线程局部批次上下文（ART 的 `ClassRedefinition::klass_` 就是同一个 `jclass` 值）；方案 C 在 Native 状态提前给受影响类和已加载子类创建全局根。外层递归事务锁把快照与原 ART 事务串行化；成功嵌套事务使外层快照失效，转回原校验。
   3. 原样调用原始 `StructurallyRedefineClasses`；成功退役保留根，失败或没有退役释放根；返回后清理上下文（RAII），返回 ART 的结果。

   不在 `Check*` 中调用 JNI/JVMTI，因为它们运行在 ART 的 `ScopedObjectAccess` 内。
3. **CheckClass**：
   1. 按 `redefinition.h` 取出 `klass` 和 `dex_file`。上下文中找不到该类时，调用原函数。
   2. 由 `DexFilePrefix` 构造 `DexView`（文件大小取自 DEX 头），用 `ReadClassDeclarations` 读取新类。读取失败或描述符不一致时，调用原函数。
   3. 先运行类声明探测，再调用 `structural_policy::Evaluate`（探测结果决定是否允许退役与拷贝成员变化）：拒绝时写 `driver->result`，记录 `lastRejection` 和 logcat，返回 false；接受时按 `ClassRedefinition*` 在线程局部保存包含退役声明的决策，记录该批次是否需永久保根；若该决策离开自有映射就会被拒绝，则标记事务依赖映射，返回 true。
4. **CheckFields / CheckMethods**：有对应决策时，按 `fields_structural` / `methods_structural` 调用 `RecordNewFieldAdded` / `RecordNewMethodAdded`，然后返回 true，`CheckMethods` 之后清除决策；没有决策时调用原函数。
5. **Capabilities**：报告 `installed`、`standardRedefineRouted`、`memberAddition`、`memberModifierChange`，以及只在已安装方案 C 中为 true 的 `methodDeletion/methodSignatureChange`；字段守卫可用时 `fieldDeletion/fieldTypeChange` 与泛称 `memberDeletion/memberTypeChange` 为 true；`hierarchyChange`、`fullDcevmComplete` 为 false。`memberMapping` 报告最近一次探测结果，`jniRetirementGuards` 统计 JNI 与 `Method.invoke` 实际拒绝退役目标的次数；外加 `diagnostic` 和 `lastRejection`（用 `support/json.h` 转义）。

映射、visitor 和方法访问守卫的事务语义见方案 C；类分配与原生提交算法继续由 ART 执行。

## 分阶段计划

| 阶段 | 内容 | 完成判据 |
| --- | --- | --- |
| 清理 | 删除调用图、选择性解释、启动门控等旧实现及其测试、工具和文档 | 已完成，旧代码见备份标签 |
| Phase 1 | 可调试前提检查；标准 `RedefineClasses` 路由到 `StructurallyRedefineClasses` | Android 16 上，独立 Agent 经标准入口新增字段和方法成功；仅方法体变化的行为不变 |
| Phase 2 | 上节的校验规则 | 修饰符变化用例提交成功并体现新语义；拒绝用例返回约定错误码且旧定义完整；无效批次整体不提交 |
| Phase 3（方案 C 加字段退役） | 普通类 concrete Java 方法删除/签名替换；字段删除、改类型与 static/实例互换；保留 ART 原提交 | Android 16 设备、探测选中 android16 布局：方法与字段的新/缓存访问（字节码、反射、JNI 含 A/V 与 `FromReflected*`、MethodHandle 含组合句柄、VarHandle）退役异常正确，CheckJNI ON 与线程强制解释执行下同样通过；保留字段值、新字段默认值、恢复与改类型、GC、父类/default 回落与旧活动帧通过；不可支持的变化整批原子拒绝 |
| Phase 4 | 继承关系和类修饰符变化，收窄前的帧检查 | 父类和接口的扩展、移除、恢复用例通过；持有受影响实例的不可去优化帧导致拒绝且旧定义完整 |
| Phase 5 | Android 11、12、13、14、15、17 逐台回归 | 每台设备的结果和 APK 哈希记录在 [VERIFICATION.md](VERIFICATION.md) |

`DcevmRuntime.initialize()` 在 Hook 全部安装成功后返回成功；尚未支持的变化以 JVMTI 错误拒绝。`capabilities()` 逐项报告已支持的变化类别，`fullDcevmComplete` 在 Phase 4 通过设备验收前保持 false。

## 风险

- **Mainline 更新**：ART APEX 会随 Play 系统更新替换，平台版本与 ART 源码版本可能不同。候选声明覆盖 11–17 源码，由运行时探测在每次事务中核对；探测不通过时保持 Phase 2 语义。新的 ART 源码版本须补充核对，确有差异时新增版本目录。
- **ART 服务语义漂移**：方案 C 保留原 Update。探测只能核对有 ART 服务或 JVMTI 作对照的声明（Class、ArtField、ArtMethod、DexCache、槽位、map、obsolete 标志、MethodHandle 偏移）；visitor 虚表顺序与来源类型、Handle 与守卫签名依赖源码声明与精确符号名；JNI 守卫只用公开 JVMTI/JNI。扩展版本时核对源码与设备行为，差异只放 `platform/abi/arm64/androidNN/`，不复制后端。
- **退役访问边界**：方案 C 保留真实旧 ArtMethod/ArtField 并守卫新访问；反射 `Field` 改指占位字段。`Unsafe` 与字段更新器缓存的原始 offset 无法拦截，任何改变实例布局的结构重定义都会使其失效。
