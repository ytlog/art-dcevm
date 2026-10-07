# 设备 ART Hook 实现迁移

## 当前架构（2026-10-07）

增强引擎现由默认 NDK 构建的 `libartdcevm.so` 提供。`source_engine/`、`build-source-engine.py`、`sources.lock.json`、`sourceEngine13` Gradle 分支及源码 SO 装载路径已删除。主库不编译 AOSP 实现或私有头文件，也不把旧源码引擎对象并入主库；锁定源码只作为自有固定声明的依据。

`evolution_runtime` 负责完整演进初始化和事务入口。外部 Agent 仍通过自己的 `jvmtiEnv::RedefineClasses` 提交整批定义；增强库的命名 Hook 代理处理演进事务，普通事务按需调用设备原函数。ART 负责原生分配、类链接、堆收集及暂停等服务。现有错误码、拒绝与回滚边界继续保留，不能以放宽校验替代对象迁移与提交。

| 自有行为 | 设备调用边界 |
| --- | --- |
| 字段、方法、类与已加载子类校验 | Hook `CheckFields/CheckMethods/CheckClass`；按名称调用设备错误记录和结构标记服务。旧标准/CompactDex 声明由自有 checked reader 读取，不为校验批量创建 JNI method ID。 |
| 层级校验、事务状态、批次分配与 verifier 临时视图 | 自有 TLS、Handle 根池和未发布替换类查询；设备原分配/链接与命名 `FindClass/LookupClass/VerifyClass` 按需调用，每次可能 GC 后重读根槽。 |
| 字段复制、退役映射、结构引用修复与发布 | 自有完整匹配、复制/清零与 monitor/card 操作、旧方法更新、反射/JNI canonical 修复；命名设备 `ReplaceReferences`、JIT 失效等操作保留原暂停顺序。 |
| 反射、JNI、MethodHandle/VarHandle、Nterp 与旧接收者守卫 | 共享退役注册表、对象解码与异常路径；自有 `Field::VisitTarget` 代理和 InstrumentationListener，类加载器销毁前清理登记。 |
| 事务预检、活动帧排空、选择性与挂起去优化 | 自有 StackVisitor、线程 scope、请求状态与监听器协调；命名设备原栈遍历、挂起与反优化函数。不安全的活动编译帧继续拒绝。 |
| 初始化校准和测试诊断 | 自有校准与 debug 诊断直接调用命名设备函数和公开 JNI/JVMTI，不回调源码模块。 |

活动编译帧由共享 `frame_recovery` 使用设备命名 CodeInfo/StackMap 解码检查实际 PC 的完整恢复映射；JIT 诊断通过设备缓存遍历确认物理代码归属。优化内联实验保留调试图及其 SSA/存活环境，只在显式编译范围内代理 HInliner 的入口内联策略，不临时切换 graph 或 Runtime 为非调试状态。恢复解码和 JIT 诊断不再使用上游 header 原始 mask；基线位经真实编译对照校准，选择性去优化按物理 carrier 的设备解码结果、Should 标志及实际 PC 恢复映射访问命名栈槽函数。具体协议及未证明的未来暂停点边界见 [单库架构](SINGLE_LIBRARY_ENGINE.md#活动编译帧与诊断编译)。

## 启用条件与验证边界

`DeviceProfile` 严格匹配目标设备已加载的四个库 GNU Build ID：`libart.so`、`libopenjdkjvmti.so`、`libdexfile.so`、平台 `libc++.so`。只有匹配固定 `android13-cas-arm64-release-baker-20261007` 声明集后，才读取私有对象；编译器实验另行核对 `libart-compiler.so`。未知构建不能仅凭相同 API 等级或同名符号启用私有增强。

命名符号由 HookEngine 解析动态或普通 ELF 符号表，内联行为由增强库固定声明实现。声明来自锁定源码，迁移时保留了静态 shape 校验与历史源码 getter 对照证据；当前初始化不编译或加载源码校准器。真实公开 JNI/JVMTI oracle 检查类/字段/方法、loader 与 JNI ID；设备 root visitor 校验真实 managed 根槽，真实平台错误记录、DEX 打开/销毁、字段存储、card 和 monitor 操作分别检查行为与所有权。平台 STL 只通过固定借用声明读取，分配对象由匹配的设备函数析构。Build ID 匹配和校准共同限定当前适配，仍不能证明所有 Heap、GC、编译路径或完整 DCEVM 语义。

无源码首版 APK `b051…` 已通过标准入口六种组合及完整 `source-first` 回归；后续最终测试仍在运行。最终验收、完整哈希和逐次证据由 [VERIFICATION.md](VERIFICATION.md) 记录。现有能力限制见 [LIMITATIONS.md](LIMITATIONS.md)。历史 `source-first` 名称及 `build/evidence/source-engine/` 目录仍沿用测试协议，不代表存在第二个演进 SO。

---

以下全文保留迁移过程中各阶段的行为说明、符号核对和历史证据。其中“当前”“仍待迁移”“源码保留”等措辞描述该阶段当时的状态，已被上面的现行架构取代；它们不是现行构建要求或剩余生产依赖清单。

## 历史：Android 13 迁移起点与分阶段记录

迁移初期（2026-10-07 删除源码引擎前），历史 `source_engine/ti_redefine.patch` 曾把下列行为编进另一个 `Redefiner` 副本；该文件现已删除。迁移时代理必须保留原事务的错误码、原子性、活动帧、对象和 JNI ID 语义；只让校验函数返回成功不能替代提交逻辑。

| 当前修改 | 设备符号和代理边界 | 迁移要求 |
| --- | --- | --- |
| 方法、字段和类校验 | `ClassRedefinition::CheckMethods/CheckFields/CheckClass` | 在自有代理中检查变更和已加载子类，向设备事务报告是否需要结构重建；失败时保持原定义。 |
| 方法与字段映射 | `ClassRedefinition::CollectNewFieldAndMethodMappings` | 对删除、签名或存储形式变化建立退役映射，刷新缓存解析，不把失效指针映射到错误的新成员。 |
| 方法元数据更新 | `ClassRedefinition::UpdateMethods` | 保留 native 绑定、旧帧和旧 JNI ID 的正确行为，并使静态/实例及可见性变化重新检查分派。该入口在设备动态符号表中可见。 |
| 字段复制与结构提交 | `ClassRedefinition::UpdateClassStructurally` | 复制仍存在且类型兼容的字段；新增或不兼容字段初始化为默认值；保留对象身份和事务回滚。源码副本中的 `CopyFields` 是内部辅助函数，不能假定有稳定的独立导出入口。 |
| 继承批次 | `Redefiner::Run`、`FinishNewClassAllocations`、`ClassLinker::AllocateClassObject` | 在代理中选择新父类和批次顺序，委托设备分配、堆遍历、暂停和类链接操作。 |
| 旧成员调用 | 已有解释器、JNI、反射、MethodHandle 等命名入口 | 继续由自有 Hook 守卫退役成员，不允许旧引用误调用新成员。 |

设备原函数包括 `Redefiner::StructurallyRedefineClasses`、`CollectAndCreateNewInstances`、`FinishNewClassAllocations`、`Run` 等。自有代理只能在各版本的 ABI 声明和运行时布局探测通过后调用它们；同名符号不能证明内部对象布局相同。`CheckMethods` 等同库内部调用由 Inline Hook 覆盖，除非逐个调用点确认其 PLT 重定位可覆盖。

## 历史：删除旧路径的验收门槛

1. 新实现不编译 `ti_redefine.cc`、AOSP 私有头文件或其他 AOSP 源文件；库和 APK 不打包源码引擎 SO。私有 ART 引用都在运行时按名字解析，主库没有强动态导入。
2. Android 13 的标准 JVMTI 双加载顺序、独立 Agent、类加载转换回调、现有结构演进套件和失败回滚全部在新 APK 上通过；回归必须包括删除成员、类型/签名变化、继承与接口变化、旧反射/JNI ID、JIT/AOT 活动帧。
3. 其他系统或厂商使用独立的符号和 ABI 声明；未验证布局或缺符号时拒绝私有增强，公开 JVMTI 路径仍可用。
4. 只有上述条件满足后，删除 `source_engine/`、`build-source-engine.py`、`sources.lock.json`、`sourceEngine13` Gradle 分支、源码模块装载和相应的测试专用入口。不能以减少构建文件为由提前删除仍承载演进语义的实现。

阶段状态：校验、层级分配／验证视图、结构提交／引用修复、成员守卫及选择性去优化已由增强库拥有。事务入口协调、Instrumentation 监听器和反射 Field 更新仍承担源码行为；源码引擎尚未完全替换。此阶段状态已由本文开头的当前架构取代。

字段校验和声明读取已移入增强库。命名 `CheckFields` Inline Hook 在演进事务内调用 `CheckEvolutionFields`，普通事务调用设备原函数。旧字段由公共 JVMTI 读取，替换 DEX 由自有解析器读取。字段、方法和类校验共用固定 `ClassRedefinition` / `DexFile` 输入路径，以 `memcpy` 访问，不解释跨库 STL 对象；过渡源码编译对照访问偏移与大小。增强库决定字段演进，写入实例成员标记并按名称调用设备 `RecordNewFieldAdded`。final 旧引用守卫的启用状态也由增强库独占，源码守卫仅回调读取；旧 final 状态及标记桥接已删除。

错误记录由增强库调用命名设备 `Redefiner::RecordFailure`。Android 13 平台 libc++ ABI v1 的长字符串声明在此调用期借用 NUL 结尾文本；原函数只同步读取两个 const 参数的 `c_str()`，不保留或析构借用对象。初始化校准用本地空事务核对短／长类签名、空／短／长错误消息、结果码及完整错误文本。这个校准仍依赖已匹配的目标设备私有布局，不能代替其他厂商的独立适配；源码错误函数已删除。

命名 `CheckMethods` 与 `CheckClass` Hook 也直接进入增强库代理。方法代理自行读取新旧 DEX 声明、匹配成员、校验完整原始修饰符并报告结构重建；类代理处理锁状态、名称、修饰符与层级声明比较，并报告错误和结构变化。旧整函数实现及修饰符回调已删除。旧 DEX 可以多类、CompactDex 或使用共享 data；增强库仅读取已由 ART 验证的声明，不校验旧 code item，也不为校验创建 JNI method ID。replacement DEX 继续严格单类校验。旧 DEX 字节区间、class-def index 和 live class flags 现由增强库通过命名 `GetMirrorClass` 和锁定源码的 Class／DexCache／DexFile 固定前缀读取，不再从源码数据桥取得。静态声明校验及每次真实事务的源码 getter 比对仍暂时保留；这是 release ObjPtr 与非 poisoned heap-reference 的已匹配设备适配。已加载子类约束已迁入增强库；需要 Handle 保护的层级解析和 changed／narrowed 事务状态也已迁入增强库；分配协调与临时验证视图仍待迁移。

结构提交对 JNI 方法 ID 的映射现在遵循 ART 的 canonical 规则：目标为 copied 方法时，增强库按命名符号调用设备 `ArtMethod::GetCanonicalMethod`；其他反射目标保留原映射。这避免 JNI ID 管理器按声明接口的数组偏移写入子类中 copied 方法的存储。回归用例显式注册全部声明方法 ID，再执行声明方法与接口默认方法的往返，防止校验读取方式改变后遗漏这条路径。

Nterp 的分发状态、invoke 指令解码及 checked resolver 调用由增强库 `nterp_dispatch.cc` 实现。代理按固定 DEX 35c/3rc 格式读取方法索引，通过命名设备符号检查缓存和未解析的调用；源码旧解码实现和状态已删除。`ClassLinker*` 现由增强库从命名 Runtime singleton 与固定声明读取；源码只转发启用状态回调，旧访问器已删除。

原地提交后的全局 JIT 缓存失效现已由增强库的 `InPlaceHook` 实现：它调用设备原提交后，按命名符号调用设备 `JitCodeCache::InvalidateAllCompiledCode`。`JitCodeCache*` 由增强库从固定 Runtime 声明读取，保留 GetJit 为空的原判断；源码访问器已删除。

源码事务调用的设备 `Redefiner::StructurallyRedefineClasses` 现由增强库按命名符号解析，并通过增强库回调供源码实现调用；源码模块不再强链接该设备入口。源码侧事务前检查与修改后的提交实现仍待迁出。

继承批次的 `Redefiner::Run` 与 `FinishNewClassAllocations` 现由增强库安装命名 Inline Hook 并实现分派代理。非演进事务调用设备原函数；演进事务按需调用旧层级实现。源码实现需要原函数行为时，回调增强库，由增强库持有并调用设备 trampoline。层级选择与缓存修复仍在源码模块，尚未达到移除条件。

`ClassRedefinition::AllocateNewClassObject` 也由同一增强库代理分派：普通事务直接调用设备原函数，有层级变化的源码演进事务才交给源码侧选择新父类。增强库只转发固定 arm64 ABI 的不透明参数，源码侧在编译时检查相关 `Handle` 与 `ObjPtr` 大小；层级实现仍待迁出。

验证期间的 `ClassVerifier::VerifyClass`、`RegTypeCache::ResolveClass`、`ClassLinker::FindClass` 与 `LookupClass` 现由增强库安装命名 Hook 并代理。`VerifyClass` 代理在源码事务中请源码回调建立临时验证视图，调用设备原函数后关闭该视图；解析代理从源码回调查询未发布的替换类，未命中时调用设备原函数。源码仍负责视图中的类型化 ART 访问与缓存刷新，尚待迁出。

`CollectAndCreateNewInstances` 由增强库安装命名 Inline Hook 并完整代理：先调用设备原函数完成收集和分配，失败时直接返回；演进事务成功后，由增强库核对初始结构批次，拒绝直接实例将继承抽象方法的替换。复制的源码分配函数、回调和导出已删除；平台 Holder／Iterator／vector<bool> 声明和真实事务只读比对仍暂时保留。

`Runtime::IsAsyncDeoptimizeable` 的 Inline Hook 现由增强库实现完整代理：先调用设备原函数；原函数拒绝时，才调用现有的可调试 AOT 栈帧安全判断。源码侧不再持有或调用原函数 trampoline；判断内部仍依赖 AOSP 类型化栈访问，须继续迁移。

`artTestSuspendFromCode` 的 Inline Hook 由增强库安装并实现完整挂起代理：先调用设备原函数，再消费匹配线程的请求，按命名符号调用设备 `Thread::PushDeoptimizationContext` 和 `artDeoptimize`。返回值、空异常和方法类型使用 Android 13 固定声明；增强库不读取 Thread 字段或栈帧偏移。源码侧的反优化处理函数与这两个强链接引用已删除。

待处理挂起反优化线程的原子状态与命中计数由增强库独占。源码侧通过纯 C 回调登记、查询、取消、重置和读取命中数。状态竞争、上下文参数和代理调用顺序已通过主机 ASan/UBSan 回归。两种 Hook 顺序的 AOT 挂起设备专项均从状态 7 的活动机器码帧切换到状态 1 的解释器帧，反优化恰好命中一次，原调用返回 14 且无异常。活动帧安全判定和请求发起仍在源码模块，结构事务也尚未迁出，不能据此删除源码模块。

源码模块的读屏障慢路径现通过增强库按名称解析的设备 `artReadBarrierMark` 调用。源码侧继续计数，并保留临时的读屏障补丁；此改动只去掉该入口的强链接引用，尚未去掉源码模块。

`ClassLinker::DeleteClassLoader` 由增强库安装命名 Inline Hook、按 Android 13 固定源码声明读取 `ClassLoaderData` 中的 allocator、清理退役成员注册表，再调用设备原销毁函数。源码侧的 `lifecycle.cc`、卸载计数器和清理回调已删除；完整类卸载路径仍需源码演进 APK 回归。

退役方法与字段的指针注册表现已移到增强库。源码侧通过纯 C 回调注册、查询和清理；字段注册表还管理反射 `Field` 使用的带标签令牌与空槽复用。增强库清理时按名称调用设备 `LinearAlloc::ContainsUnsafe`，不传递 C++ 容器跨越两个库的 ABI。类型化 ART 访问守卫仍在源码模块。

## 历史：目标设备符号核对（2026-10-07）

在 Android 13 真机 `DEVICE_001` 上读取 `/apex/com.android.art/lib64/` 的设备库，Build ID 分别为 `libopenjdkjvmti.so` 的 `2fe21c45581b9c8b0519b71cbde32938` 和 `libart.so` 的 `14fc77b24db0b351b919c97009dc86bf`。用 NDK `llvm-readelf --dyn-syms --wide` 核对动态符号表：

- `ClassRedefinition::RecordNewFieldAdded`、`RecordNewMethodAdded`、`CheckFields`、`CheckMethods`、`CollectNewFieldAndMethodMappings` 与 `UpdateClassStructurally` 均有命名导出。前两个可供自有校验代理报告结构变化；名称存在仍不能代替事务对象布局探针。
- `libart.so` 导出 `jit::JitCodeCache::InvalidateAllCompiledCode` 和 `LinearAlloc::ContainsUnsafe`。取得接收者对象仍需固定源码声明与运行时校验，不能从二进制推算成员偏移。
- `CopyFields` 没有独立动态导出。字段删除和类型变化不能通过只 Hook `CheckFields` 来实现：设备提交仍按旧字段都存在于新布局的前提复制；必须在自有 `UpdateClassStructurally` 代理中处理字段匹配、默认值和回滚边界，并保持旧引用失效语义。

设备库副本仅保存在已忽略的 `build/evidence/device-symbols/20261007/`，不作为构建输入或提交内容。

同一设备另有 `UNSAFE.class.structurally_redefine_class_direct` JVMTI 扩展。隔离探针在新增字段后尝试由此删除字段，两种 Agent 装载顺序均返回 `JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED`，因此该扩展不能替代自有删除事务代理；详见 `docs/VERIFICATION.md` 的直接扩展探针记录。

## 历史：结构复制与退役核心守卫迁移（2026-10-07）

增强库拥有 `CopyAndClearEvolutionFields` 的完整复制调度：对每个旧声明类，从新层级顶端按类加载器和 descriptor 搜索匹配类，在同一 static／instance 存储中按名字与完整类型匹配字段。删除、类型变化和存储互换的旧字段不复制；新增或新父类字段保持原分配状态。完成全部复制后移交 monitor，对目标执行写屏障，再清零所有旧字段并对源执行写屏障。此路径无分配、无递归、无 STL 跨库对象；源码五个旧复制 helper 的实现已删除。字段元数据、原始值读写、monitor 操作和 GC card 标记也已迁入增强库；旧适配器删除，源码只保留提交入口转发和初始化声明校准。结构事务整体仍需迁移。

退役方法／字段的核心守卫现由增强库拥有：查询自有注册表，对退役成员清零完整八字节 JValue，并按命名设备 `Thread::ThrowNewException` 抛出原来的 NoSuchMethodError／NoSuchFieldError。缺省线程通过命名 `Thread::CurrentFromGdb` 查询；异常交付通过命名 `QuickDeliverException`。增强 Hook 直接调用自有方法守卫，源码 JNI／反射／MethodHandle 解码后转发守卫回调；对应两个源码核心导出已删除。字段槽计数也直接读取增强注册表，删除旧计数桥和未使用的源码汇总导出。类型化解码、接收者与 final 检查仍需迁移。

## 历史：字段复制底层访问迁移（2026-10-07）

增强库按固定 Class／ArtField／LengthPrefixedArray 声明枚举字段，按命名设备 `ArtField::GetName` / `GetTypeDescriptor` 读取文本；类 descriptor 自行从标准／CompactDex 的 native mapping 读取，Object 根类使用固定 descriptor。父类和 loader 通过自有 Baker reader 获取，保留灰对象标记后的身份，避免比较 from-space 与 to-space loader 引用。普通 getter 的 monitor acquire、灰对象命名 `artReadBarrierMark`、普通／volatile 引用读取与默认不 CAS 回写语义已保留。

字段存储按源码 offset 及 volatile flags 实现 1／2／4／8 字节 relaxed／seq_cst 原子读写，浮点数保持原始位；引用写入遵循已校准的未 poisoned HeapReference32 编码，非 null 值标记 owner card。card 地址来自命名 `Thread::CurrentFromGdb` 和固定源码 `Thread::CardTableOffset<8>()` 的 biased pointer，shift 与 dirty 值均对照锁定源码声明，不从二进制推断。源／目标的最终全字段写屏障与原 monitor 转移顺序保持一致。

初始化探针对照真实声明类／父类链、所有实例和静态字段、实际字段值及 Thread card table；对私有临时对象做 primitive 写入／清零与 card 校准。monitor 的整个 LockWord 只能在原结构提交的暂停边界中迁移，校准使用对齐的 native headers，避免在共享锁下移植真实对象的 GC 状态。源码校准继续验证 release Baker、ObjPtr／heap reference 非 poisoned、所有字段布局、数组 stride 和 monitor/card 常量，尚待最终去除。旧计数 wrapper 和未使用的 field registry count／slot callbacks 也已删除。

## 历史：已加载类约束、实例检查与反射解码迁移（2026-10-07）

增强库通过命名 `ClassLinker::VisitClasses` 遍历已加载类，保留 final 类子类拒绝、方法 final override 与 abstract implementation 约束。固定 Class／ArtMethod／IfTable 声明处理父类、接口、成员访问和 copied virtual methods；native declaring-class GcRoot 使用命名 `artReadBarrierForRootSlow`，不把它当作托管对象字段。方法名与完整 signature 从声明类 DEX 读取，不生成 JNI ID。遍历持有 ClassLinker 锁，增强 visitor 无分配、无 JNI、无挂起；源码旧判断循环删除。ClassLinker receiver 与 Nterp 共用增强库 RuntimeAccess，源码访问器已删除。

实例代理完整委托设备原收集／分配行为，只增加分配成功后的抽象方法检查。固定借用的 Holder、Iterator、Handle 和平台 bit-vector 声明不调用 NDK STL 操作；Handle 每次读取 rooted slot，遵循源码无 heap barrier 的 StackReference 语义，托管数组使用增强 Baker reader。初始结构批次、类和实例数组及抽象类外部 vtable 均与真实源码 getter 比对。删除两处已失去入口的复制 allocator，避免保留不再参与生产事务的 AOSP 默认分配实现。

`mirror::Field::GetArtField` 现由增强库 Hook 自行读取 native 字段数组或解析注册表退休令牌。源码 Field visitor 使用设备命名 getter，运行时进入增强 Hook；旧成员实现和导出删除。Runnable 反射方法守卫调用命名 `ArtMethod::FromReflectedMethod` 后进入增强核心 Guard；JNI Native scope、接收者与 MethodHandle 操作仍在源码。校准仅读取已登记成员的反射对象和真实解码结果。

测试用的读屏障诊断累计源码和增强库各自真实 gray-owner marker 调用；增强计数只编入测试构建。迁移后不能继续仅统计源码慢路径，否则并发 GC 验收会遗漏增强 getter 已执行的屏障。源码构建的过渡 Gradle 输入也包含共享固定声明头，避免修改 C callback ABI 后仍沿用旧 SO。结构发布／回滚、层级视图、引用修复及活动帧处理仍需迁出，源码构建和独立 SO 尚未删除。

## 历史：层级校验、Runtime 服务与 JNI scope（2026-10-07）

增强库完整处理父类解析、访问／final／interface／cycle／初始化约束、旧传递接口保留和 direct-interface 顺序变化，拥有可嵌套恢复的 changed／narrowed TLS。命名 FindClass 可以分配和挂起，因此当前类、loader、父类及新接口放入固定源码 HandleScope 根链，每次解析后重新读取槽。固定 Thread top-handle／exception 声明及根链结构通过静态校准和持共享 mutator lock 的真实 push／pop 校准；错误时保持原 pending exception 清理语义。源 HierarchyTransactionScope 和 Has 状态仅转发 POD 回调，旧完整校验与 source TLS 已删除。分配上下文和 receiver guard 的布尔查询仍为过渡接口。

RuntimeAccess 按名称解析设备 Runtime::instance_，使用锁定 Android 13 的 release prefix 读取 ClassLinker、JniIdManager、JIT 和 code cache。平台 string／vector／list／unique_ptr 只用固定尺寸的借用声明描述布局，不把 NDK STL 对象传给平台。源 friend 仅用于编译静态校准和真实 getter 比对；没有从二进制推算字段。旧 ClassLinker 返回桥和 JIT cache getter 删除。JNI ID 低位 tag 为 index 时调用命名设备 JniIdManager::DecodeMethodId／DecodeFieldId，否则指针原样返回；初始化分别核对实际 ID、pointer ID 和 null。

NativeObjectAccess 使用命名 artJniMethodEnd／Start 委托设备完成 Native↔Runnable 转换、checkpoint、GC flip 和锁记账，Runnable 嵌套不转换。JNI receiver 与 thread state 使用固定源码声明；原 pending exception、local segment 和 cookie 保持。被 Hook 的 Field 18 个 get／set 和 Constructor.newInstance0 在锁定注册表及设备真实 flags 中均为 FAST_NATIVE，合法反射调用在 Runnable；JNI 表入口支持 Native 与 Runnable。增强库拥有反射与方法／字段 ID 的 Native／Runnable 退休守卫，源码旧 wrapper 及 dlsym 删除。Method registry 为空时仍不解码 ID，没有引入重复状态。MethodHandle、VarHandle、receiver 与 Nterp final-field 规则仍待迁出。

结构提交的旧方法处理也由增强库完整拥有：按 name／signature／static 找 replacement，保留仍为 native 的绑定，跳过声明类仍为活接口的 copied defaults，按原 relaxed fetch_or 设置 obsolete／dont-compile，并命名调用设备 SetEntryPointsForObsoleteMethod。JNI canonicalization 的 copied／intrinsic 重叠位判断也迁到增强库。source callback 不传平台 map。复制的 UpdateClass／UpdateClassInPlace／UpdateMethods／UpdateFields／FindAndAllocateObsoleteMethods 已无生产入口，与独占 obsolete visitor、mapping 和名字签名 helper 一起删除；设备原地提交行为继续委托原函数。结构引用修复／发布与回滚、层级分配／验证视图及 selective deopt 仍承担源码行为，尚不能删除源码构建或 SO。


## 历史：结构提交、层级分配与编译帧代理（2026-10-07）

结构提交代理现由增强库拥有。增强库建立方法／字段退役映射和对象替换表，按原暂停边界复制字段、更新方法与反射／JNI 目标，调用设备命名 ReplaceReferences，随后保留旧类、失效 JIT 并清空解释器缓存。平台 const 对象表使用锁定 libc++ 节点、桶与指针 hash 声明借用，不把 NDK unordered_map 传入 ART。设备原结构事务仍负责收集、暂停和发布边界；源码复制的提交、映射和辅助更新实现已删除。

层级批次使用增强库 Handle 根池，递归按父类优先分配，复用设备原分配入口。根池通过 anchor link 扩展，避免覆盖设备临时 HandleScope；解析返回后重读根槽。验证视图优先当前祖先链，再使用同 loader 的批次替换；缓存刷新只针对明确的未发布 DexCache。源码分配算法、查找视图与诊断转发删除，事务 Scope 暂时保留状态回调。

MethodHandle／VarHandle、JNI 接收者和 Nterp final 写入规则迁入增强库，复用统一退役注册表、对象解码与异常路径。Runtime 监听器仍在源码，仅读取增强库拥有的 guard 状态并调用已有 guard；它是下一阶段的剩余生产行为。

增强库使用固定 StackVisitor 声明及设备命名构造／WalkStack／GetMethod 调用栈遍历，保留平台析构和上下文生命周期。异步去优化代理先调用原函数，再检查应用 AOT 帧；选择性去优化使用设备 InstrumentThreadStack，按锁定 instrumentation stack 树结构定位记录，通过字节复制写入 force_deopt_id。不依据反汇编推断偏移或地址。静态与真实设备校准暂时仍编入源码模块。

剩余生产行为为事务入口的预检／guard 回滚／活动帧排空／挂起去优化协调、InstrumentationListener 的事件处理与注册、mirror::Field::VisitTarget 的反射对象更新。其后还须迁移测试和校准支持，删除源码编译、Gradle property、独立 SO 与装载分支；当前通过回归不代表完整迁移目标已经完成。
