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

### 2026-10-13 Phase 3/4 源码与逐库核对

**结论：核心提交服务有命名符号支持，但目前不能证明自有提交能在全部 27 套库上仅凭现有固定声明正确运行，也不能按原“墓碑映射加两个 JNI 解码 Hook”方案承诺完整 Phase 3。** 尚有 vivo 锁 ABI、源码内联/模板声明、跨运行时容器 ABI 和退役访问语义缺口。Phase 4 还需处理父类/接口解析、批次拓扑与分配声明。此次只调查、扩充符号目录与核对工具，未修改引擎或增加已支持能力。

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

公共目录现有 **49 个精确名称**：47 个要求 `.dynsym`，两个允许命名局部表；27/27 套全部通过。保留原 17 个名称，新增 31 个全套 `.dynsym` 服务及 `CopyFields` 候选。标准 JVMTI 表使用的 `JvmtiFunctions::RedefineClasses` 包装器和 `CopyFields` 均为 G27；前者的调用覆盖已经有 Phase 1–2 设备证据，后者尚未安装或验证。其余公共目录服务在 API 30–37 的每一套均为 D。**未加入**只覆盖部分库的初始化、Mutex Lock、`LoadSuperAndInterfaces`、`ComputeClassSize` 和偶然生成的字段访问器。

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

**自有提交的执行步骤有主要命名服务，但“在所有样本上完整可行”还不成立。** 27 套库存只证明名称，不证明固定声明、返回类型、函数对象或容器 ABI。当前 `platform/abi/arm64/common/redefinition.h` 只含三个前缀，至少需要补齐源代码对应的 Redefiner runtime/self、Holder/Iter 槽与标志、ObjPtr/Handle、Class/ObjectArray、ArtField/ArtMethod、reflective visitor、Runtime/Jit owner、Thread cache、ObjectLock/Mutex、锁字/GC 屏障，以及 ART `std::__1::map/unordered_map` 与 NDK `std::__ndk1` 的跨边界兼容声明/容器策略。共同内容放 common，确有差异才放 androidNN；ART Mainline 与 OEM 构建需按实际库证据选择，不能用 API 对象偏移猜测。

必须保留 [ART Run 的最终事务](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/openjdkjvmti/ti_redefine.cc#2499)：从“不可失败点”起不再分配 Java 对象或调用可能挂起的 JNI/JVMTI；所有新增目标、根与校验提前完成。原 ART 提供 GC 停止、暂停线程、实例创建与回滚前置步骤；自有逻辑只改兼容成员匹配与字段拷贝决策，按源码顺序调用 ART 引用改写、去优化、obsolete 和失效服务。引用替换前保存旧 Class 集合，替换后按该版源码修复旧成员 declaring class、保活旧 Class，再失效 JIT 和同步清缓存。11 与 13+ 的 shadowing/DexCache 判定优先级不同，旧 native 入口复制还假设新方法同签名且仍是 native，删除/签名变化必须改正该决策。

有一个更窄的备选：`CopyFields` 的完整命名符号为 G27，可以调查只接管字段匹配/复制而保留原 `UpdateClassStructurally` 的锁字、清空、屏障和全套提交。源码的递归体存在**不证明首次字段拷贝没有内联**；需逐版本设备证明静态、当前类和祖先字段拷贝均经过 Hook，以及映射 Hook 覆盖，才能采用。它也不解决原 native 复制假设或退役访问语义，因此此次只加入候选声明。

**完整 Phase 3 必须先调整退役访问方案。** [Field 的 reflective visitor](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/mirror/field.cc#28) 要求目标属于声明类的真实字段数组，并复制 offset；[反射 Field.get](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/native/java_lang_reflect_Field.cc#78)、[MethodHandle 字段访问](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/method_handles.cc#621)、[VarHandle 字段访问](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/mirror/var_handle.cc#1661) 直接读写字段 offset。ArtField 没有能抛异常的方法入口；有效墓碑字段仍会读写，无效字段或 null 会崩溃，不能自然产生 NoSuchFieldError。需要重新核对这些访问边界可用的命名守卫/语义，或继续拒绝字段退役，不能宣称一个映射覆盖全部异常行为。

同样，[JNI 内联解码](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/jni/jni_internal.h#84) 对 pointer ID 直接 cast，索引 ID 才经过 manager；即使 manager 被 Hook，[调用方](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/jni/jni_internal.cc#1552) 解码后立即解引用，不检查 null 或 pending exception。在 decoder 抛异常后返回 null 不安全，返回普通墓碑仍会读写。仅要求启动时 debuggable 也不能作为这些路径已覆盖的证明。方法墓碑还需真实链接成员、正确分派和抛异常入口的运行证据。

**Phase 4 可继续命名服务路线，但必须扩展解析策略。** 原 allocator 明言层级未变化；完整新层级需要 [LoadSuperAndInterfaces](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/class_linker.cc#6665) 的自循环、可访问性、父类/接口解析语义。缺独立符号时，可研究固定源码包装 DoResolveType 和新 DEX resolved cache；外部类型在暂停类加载前解析，批次父类/接口按新拓扑映射，保持 LinkClass 生成 vtable/iftable。LinkClass 的父类检查不能代替全部接口可访问性校验。不要直接改 super 后假定接口已加载，也不要调用会公开新类的 DefineClass 代替事务内 stripped allocation。自有 Finish 的 ObjectArray/handle 模板可声明源码包装调用 ART 现存分配服务，或保留原 Finish/Allocate 辅助路径，不重新实现分配器。

复核命令：

```sh
python3 tools/check-structural-symbols.py
python3 tools/check-structural-symbols.py --audit-spec tools/structural-symbol-audit.json --report build/evidence/structural-symbols/phase34-audit.json
python3 tools/test-structural-symbol-audit.py
python3 tools/test-elf-mini-inventory.py
```

完整逐套报告记录每个精确名称的三表位置/缺失、库 SHA-256、审计清单与目录 SHA-256。原始源码、全部名字库存、测试日志与 JSON 报告仅放在已忽略的 `build/evidence/structural-symbols/`，不提交运行库、设备清单或本机配置。公共目录严格检查失败时返回非零；研究候选的缺失被完整报告，不降低目录要求。运行时仍由官方 ShadowHook 按目录限定表范围查找；`.gnu_debugdata` 是命名 ELF 表，未反汇编、读取符号地址、计算对象偏移或证明运行行为。

## 接管点

| ART 决策点 | ART 原行为 | 增强库行为 |
| --- | --- | --- |
| 公开表的 `JvmtiFunctions::RedefineClasses` | 经 `Redefiner::RedefineClasses` 的普通重定义，结构变化一律拒绝；导出入口可能被内联 | 路由到原始 `Redefiner::StructurallyRedefineClasses`，保留调用者的 `jvmtiEnv*`、能力检查和 `ClassFileLoadHook`。仅方法体变化时 ART 仍走原地更新路径，行为不变 |
| `CheckClass` | 修饰符、父类、接口不得变化 | 自有策略：允许修饰符和继承关系变化，冲突时（加 final 与已加载子类冲突等）拒绝 |
| `CheckFields` / `CheckMethods` | 只允许新增，修饰符不得变化 | 自有策略：允许删除、类型和签名变化、修饰符变化；有任何结构变化时调用 `RecordNewFieldAdded` / `RecordNewMethodAdded`，确保 ART 走结构路径 |
| `CollectNewFieldAndMethodMappings` | 找不到同名同签名方法时 `CHECK` 失败 | 自有映射按身份匹配；退役访问方案须先修订，见符号证据 |
| `UpdateClassStructurally` | 字段首轮拷贝可能内联 `CopyFields`（命名递归体仍存在）；缺失字段或不一致父类链导致 `CHECK` 失败 | 自有提交，按 ART 源码顺序调用命名的 `VisitReflectiveTargets`、`DeoptimizeAllThreadFrames`、`ReplaceReferences` 等服务；只替换字段拷贝和映射策略 |
| `AllocateNewClassObject` / `FinishNewClassAllocations` | 父类取自旧类 | 计划先解析新父类/接口与批次拓扑，再调用原生分配/链接服务，见符号证据 |

## 语义

- **新增字段**：已有对象取零值或 `null`，不重新执行构造器；新建对象执行新构造器。
- **删除字段、改字段类型、static 与实例互换**：旧字段退役，新字段从零值开始。兼容字段保留原值。
- **删除方法、改签名**：旧方法退役，调用时抛 `NoSuchMethodError`；删除覆写方法后，虚调用按新 vtable 分派到父类实现。
- **退役引用（目标，方案待修订）**：继续复用 `VisitReflectiveTargets` 改写合法成员目标与 DexCache；但字段指针替换不会自然抛异常，JNI 解码也有旁路与非空返回假设。必须先证明反射、JNI、MethodHandle、VarHandle 全部访问语义；目前不承诺“墓碑映射加两个解码 Hook”足够，详见符号证据。
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
   2. 直接调用官方 `shadowhook_init()` 初始化，再通过 `symbols::Find()` 解析 MVP 用到的 7 个符号：公开表的 `JvmtiFunctions::RedefineClasses`、结构入口、三个校验点、两个 `RecordNew*Added`。任一缺失就失败，并写明缺失的符号；公开表与命名包装器不一致时也失败。
   3. 保存结构入口和三个校验点的原始地址；直接调用官方 `shadowhook_hook_sym_addr()` 替换标准包装器和三个校验点，标准包装器最后安装。失败信息取 `shadowhook_get_errno()` / `shadowhook_to_errmsg()`；任一步失败，直接调用 `shadowhook_unhook()` 逆序撤销已安装的部分，不再另设 Hook API 封装。
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
| Phase 3 | 自有映射与提交：成员删除、类型和签名变化；退役访问方案先按符号证据修订 | 删除和类型变化用例通过；退役引用在反射、JNI、MethodHandle、VarHandle 上抛出约定异常 |
| Phase 4 | 继承关系和类修饰符变化，收窄前的帧检查 | 父类和接口的扩展、移除、恢复用例通过；持有受影响实例的不可去优化帧导致拒绝且旧定义完整 |
| Phase 5 | Android 11、12、13、14、15、17 逐台回归 | 每台设备的结果和 APK 哈希记录在 [VERIFICATION.md](VERIFICATION.md) |

`DcevmRuntime.initialize()` 在 Hook 全部安装成功后返回成功；尚未支持的变化以 JVMTI 错误拒绝。`capabilities()` 逐项报告已支持的变化类别，`fullDcevmComplete` 在 Phase 4 通过设备验收前保持 false。

## 风险

- **Mainline 更新**：ART APEX 会随 Play 系统更新替换。只在 `ClassRedefinition` 前缀等已有固定声明之上读写字段，被 Hook 函数的其他参数原样传回 ART；每次加入新的设备库都运行符号核对工具。
- **ART 服务语义漂移**：自有 `UpdateClassStructurally` 必须逐版本对照 AOSP 源码的调用顺序；某版本的服务签名不同时，在 `platform/abi/arm64/androidNN/` 增加声明，不复制整套实现。
- **墓碑成员的边界**：墓碑方法和字段的形状必须保证任何调用路径都只会抛出异常，不会按错误布局读写对象。
