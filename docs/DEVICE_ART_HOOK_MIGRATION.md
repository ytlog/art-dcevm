# 设备 ART Hook 实现迁移

目标：`artdcevm` 只编译自有的代理与兼容代码。运行时通过命名 ELF 符号调用设备的 ART/JVMTI 原函数；需要改变行为的入口由 Inline Hook 或确实覆盖全部调用点的 PLT Hook 代理。Gradle 不下载或编译 AOSP 源码，也不生成 `libartdcevm_source13.so`。标准 `jvmtiEnv::RedefineClasses` 继续是外部 Agent 的入口。

## Android 13 当前替换边界

目前 `source_engine/ti_redefine.patch` 把下列行为编进了另一个 `Redefiner` 副本。迁移时代理必须保留原事务的错误码、原子性、活动帧、对象和 JNI ID 语义；只让校验函数返回成功不能替代提交逻辑。

| 当前修改 | 设备符号和代理边界 | 迁移要求 |
| --- | --- | --- |
| 方法、字段和类校验 | `ClassRedefinition::CheckMethods/CheckFields/CheckClass` | 在自有代理中检查变更和已加载子类，向设备事务报告是否需要结构重建；失败时保持原定义。 |
| 方法与字段映射 | `ClassRedefinition::CollectNewFieldAndMethodMappings` | 对删除、签名或存储形式变化建立退役映射，刷新缓存解析，不把失效指针映射到错误的新成员。 |
| 方法元数据更新 | `ClassRedefinition::UpdateMethods` | 保留 native 绑定、旧帧和旧 JNI ID 的正确行为，并使静态/实例及可见性变化重新检查分派。该入口在设备动态符号表中可见。 |
| 字段复制与结构提交 | `ClassRedefinition::UpdateClassStructurally` | 复制仍存在且类型兼容的字段；新增或不兼容字段初始化为默认值；保留对象身份和事务回滚。源码副本中的 `CopyFields` 是内部辅助函数，不能假定有稳定的独立导出入口。 |
| 继承批次 | `Redefiner::Run`、`FinishNewClassAllocations`、`ClassLinker::AllocateClassObject` | 在代理中选择新父类和批次顺序，委托设备分配、堆遍历、暂停和类链接操作。 |
| 旧成员调用 | 已有解释器、JNI、反射、MethodHandle 等命名入口 | 继续由自有 Hook 守卫退役成员，不允许旧引用误调用新成员。 |

设备原函数包括 `Redefiner::StructurallyRedefineClasses`、`CollectAndCreateNewInstances`、`FinishNewClassAllocations`、`Run` 等。自有代理只能在各版本的 ABI 声明和运行时布局探测通过后调用它们；同名符号不能证明内部对象布局相同。`CheckMethods` 等同库内部调用由 Inline Hook 覆盖，除非逐个调用点确认其 PLT 重定位可覆盖。

## 删除旧路径的验收门槛

1. 新实现不编译 `ti_redefine.cc`、AOSP 私有头文件或其他 AOSP 源文件；库和 APK 不打包源码引擎 SO。私有 ART 引用都在运行时按名字解析，主库没有强动态导入。
2. Android 13 的标准 JVMTI 双加载顺序、独立 Agent、类加载转换回调、现有结构演进套件和失败回滚全部在新 APK 上通过；回归必须包括删除成员、类型/签名变化、继承与接口变化、旧反射/JNI ID、JIT/AOT 活动帧。
3. 其他系统或厂商使用独立的符号和 ABI 声明；未验证布局或缺符号时拒绝私有增强，公开 JVMTI 路径仍可用。
4. 只有上述条件满足后，删除 `source_engine/`、`build-source-engine.py`、`sources.lock.json`、`sourceEngine13` Gradle 分支、源码模块装载和相应的测试专用入口。不能以减少构建文件为由提前删除仍承载演进语义的实现。

当前状态：以上代理尚未完成。这个文件记录迁移边界，不表示现有源码引擎已被替换。

字段校验和声明读取已移入增强库。命名 `CheckFields` Inline Hook 在演进事务内调用 `CheckEvolutionFields`，普通事务调用设备原函数。旧字段由公共 JVMTI 读取，替换 DEX 由自有解析器读取。字段、方法和类校验共用固定 `ClassRedefinition` / `DexFile` 输入路径，以 `memcpy` 访问，不解释跨库 STL 对象；过渡源码编译对照访问偏移与大小。增强库决定字段演进，写入实例成员标记并按名称调用设备 `RecordNewFieldAdded`。final 旧引用守卫的启用状态也由增强库独占，源码守卫仅回调读取；旧 final 状态及标记桥接已删除。

错误记录由增强库调用命名设备 `Redefiner::RecordFailure`。Android 13 平台 libc++ ABI v1 的长字符串声明在此调用期借用 NUL 结尾文本；原函数只同步读取两个 const 参数的 `c_str()`，不保留或析构借用对象。初始化校准用本地空事务核对短／长类签名、空／短／长错误消息、结果码及完整错误文本。这个校准仍依赖已匹配的目标设备私有布局，不能代替其他厂商的独立适配；源码错误函数已删除。

命名 `CheckMethods` 与 `CheckClass` Hook 也直接进入增强库代理。方法代理自行读取新旧 DEX 声明、匹配成员、校验完整原始修饰符并报告结构重建；类代理处理锁状态、名称、修饰符与层级声明比较，并报告错误和结构变化。旧整函数实现及修饰符回调已删除。旧 DEX 可以多类、CompactDex 或使用共享 data；增强库仅读取已由 ART 验证的声明，不校验旧 code item，也不为校验创建 JNI method ID。replacement DEX 继续严格单类校验。旧 DEX 字节区间、class-def index 和 live class flags 现由增强库通过命名 `GetMirrorClass` 和锁定源码的 Class／DexCache／DexFile 固定前缀读取，不再从源码数据桥取得。静态声明校验及每次真实事务的源码 getter 比对仍暂时保留；这是 release ObjPtr 与非 poisoned heap-reference 的已匹配设备适配。已加载子类约束及需要 Handle 保护的层级解析仍在源码窄桥，必须继续迁移。

结构提交对 JNI 方法 ID 的映射现在遵循 ART 的 canonical 规则：目标为 copied 方法时，增强库按命名符号调用设备 `ArtMethod::GetCanonicalMethod`；其他反射目标保留原映射。这避免 JNI ID 管理器按声明接口的数组偏移写入子类中 copied 方法的存储。回归用例显式注册全部声明方法 ID，再执行声明方法与接口默认方法的往返，防止校验读取方式改变后遗漏这条路径。

Nterp 的分发状态、invoke 指令解码及 checked resolver 调用由增强库 `nterp_dispatch.cc` 实现。代理按固定 DEX 35c/3rc 格式读取方法索引，通过命名设备符号检查缓存和未解析的调用；源码旧解码实现和状态已删除。过渡源码仅在初始化时提供 `ClassLinker*` 并转发启用状态回调，仍待移除该访问器。

原地提交后的全局 JIT 缓存失效现已由增强库的 `InPlaceHook` 实现：它调用设备原提交后，按命名符号调用设备 `JitCodeCache::InvalidateAllCompiledCode`。过渡期源码模块仅返回 `JitCodeCache*`；这条路径仍需在最终迁移中用固定声明和布局探针去掉该访问器。

源码事务调用的设备 `Redefiner::StructurallyRedefineClasses` 现由增强库按命名符号解析，并通过增强库回调供源码实现调用；源码模块不再强链接该设备入口。源码侧事务前检查与修改后的提交实现仍待迁出。

继承批次的 `Redefiner::Run` 与 `FinishNewClassAllocations` 现由增强库安装命名 Inline Hook 并实现分派代理。非演进事务调用设备原函数；演进事务按需调用旧层级实现。源码实现需要原函数行为时，回调增强库，由增强库持有并调用设备 trampoline。层级选择与缓存修复仍在源码模块，尚未达到移除条件。

`ClassRedefinition::AllocateNewClassObject` 也由同一增强库代理分派：普通事务直接调用设备原函数，有层级变化的源码演进事务才交给源码侧选择新父类。增强库只转发固定 arm64 ABI 的不透明参数，源码侧在编译时检查相关 `Handle` 与 `ObjPtr` 大小；层级实现仍待迁出。

验证期间的 `ClassVerifier::VerifyClass`、`RegTypeCache::ResolveClass`、`ClassLinker::FindClass` 与 `LookupClass` 现由增强库安装命名 Hook 并代理。`VerifyClass` 代理在源码事务中请源码回调建立临时验证视图，调用设备原函数后关闭该视图；解析代理从源码回调查询未发布的替换类，未命中时调用设备原函数。源码仍负责视图中的类型化 ART 访问与缓存刷新，尚待迁出。

`CollectAndCreateNewInstances` 也已改由增强库安装命名 Inline Hook 并分派：非演进事务调用设备原函数，演进事务调用现有实例收集实现。源码实现需要设备原分配步骤时，回调增强库；源码侧不再持有设备 trampoline。

`Runtime::IsAsyncDeoptimizeable` 的 Inline Hook 现由增强库实现完整代理：先调用设备原函数；原函数拒绝时，才调用现有的可调试 AOT 栈帧安全判断。源码侧不再持有或调用原函数 trampoline；判断内部仍依赖 AOSP 类型化栈访问，须继续迁移。

`artTestSuspendFromCode` 的 Inline Hook 由增强库安装并实现完整挂起代理：先调用设备原函数，再消费匹配线程的请求，按命名符号调用设备 `Thread::PushDeoptimizationContext` 和 `artDeoptimize`。返回值、空异常和方法类型使用 Android 13 固定声明；增强库不读取 Thread 字段或栈帧偏移。源码侧的反优化处理函数与这两个强链接引用已删除。

待处理挂起反优化线程的原子状态与命中计数由增强库独占。源码侧通过纯 C 回调登记、查询、取消、重置和读取命中数。状态竞争、上下文参数和代理调用顺序已通过主机 ASan/UBSan 回归。两种 Hook 顺序的 AOT 挂起设备专项均从状态 7 的活动机器码帧切换到状态 1 的解释器帧，反优化恰好命中一次，原调用返回 14 且无异常。活动帧安全判定和请求发起仍在源码模块，结构事务也尚未迁出，不能据此删除源码模块。

源码模块的读屏障慢路径现通过增强库按名称解析的设备 `artReadBarrierMark` 调用。源码侧继续计数，并保留临时的读屏障补丁；此改动只去掉该入口的强链接引用，尚未去掉源码模块。

`ClassLinker::DeleteClassLoader` 由增强库安装命名 Inline Hook、按 Android 13 固定源码声明读取 `ClassLoaderData` 中的 allocator、清理退役成员注册表，再调用设备原销毁函数。源码侧的 `lifecycle.cc`、卸载计数器和清理回调已删除；完整类卸载路径仍需源码演进 APK 回归。

退役方法与字段的指针注册表现已移到增强库。源码侧通过纯 C 回调注册、查询和清理；字段注册表还管理反射 `Field` 使用的带标签令牌与空槽复用。增强库清理时按名称调用设备 `LinearAlloc::ContainsUnsafe`，不传递 C++ 容器跨越两个库的 ABI。类型化 ART 访问守卫仍在源码模块。

## 目标设备符号核对（2026-10-07）

在 Android 13 真机 `DEVICE_001` 上读取 `/apex/com.android.art/lib64/` 的设备库，Build ID 分别为 `libopenjdkjvmti.so` 的 `2fe21c45581b9c8b0519b71cbde32938` 和 `libart.so` 的 `14fc77b24db0b351b919c97009dc86bf`。用 NDK `llvm-readelf --dyn-syms --wide` 核对动态符号表：

- `ClassRedefinition::RecordNewFieldAdded`、`RecordNewMethodAdded`、`CheckFields`、`CheckMethods`、`CollectNewFieldAndMethodMappings` 与 `UpdateClassStructurally` 均有命名导出。前两个可供自有校验代理报告结构变化；名称存在仍不能代替事务对象布局探针。
- `libart.so` 导出 `jit::JitCodeCache::InvalidateAllCompiledCode` 和 `LinearAlloc::ContainsUnsafe`。取得接收者对象仍需固定源码声明与运行时校验，不能从二进制推算成员偏移。
- `CopyFields` 没有独立动态导出。字段删除和类型变化不能通过只 Hook `CheckFields` 来实现：设备提交仍按旧字段都存在于新布局的前提复制；必须在自有 `UpdateClassStructurally` 代理中处理字段匹配、默认值和回滚边界，并保持旧引用失效语义。

设备库副本仅保存在已忽略的 `build/evidence/device-symbols/20261007/`，不作为构建输入或提交内容。

同一设备另有 `UNSAFE.class.structurally_redefine_class_direct` JVMTI 扩展。隔离探针在新增字段后尝试由此删除字段，两种 Agent 装载顺序均返回 `JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED`，因此该扩展不能替代自有删除事务代理；详见 `docs/VERIFICATION.md` 的直接扩展探针记录。

## 结构复制与退役核心守卫迁移（2026-10-07）

增强库拥有 `CopyAndClearEvolutionFields` 的完整复制调度：对每个旧声明类，从新层级顶端按类加载器和 descriptor 搜索匹配类，在同一 static／instance 存储中按名字与完整类型匹配字段。删除、类型变化和存储互换的旧字段不复制；新增或新父类字段保持原分配状态。完成全部复制后移交 monitor，对目标执行写屏障，再清零所有旧字段并对源执行写屏障。此路径无分配、无递归、无 STL 跨库对象；源码五个旧复制 helper 的实现已删除。过渡适配器仍提供借用元数据、ART 类型化字段读写、monitor 操作和 GC 写屏障，结构事务整体与这些低层操作仍需迁移。

退役方法／字段的核心守卫现由增强库拥有：查询自有注册表，对退役成员清零完整八字节 JValue，并按命名设备 `Thread::ThrowNewException` 抛出原来的 NoSuchMethodError／NoSuchFieldError。缺省线程通过命名 `Thread::CurrentFromGdb` 查询；异常交付通过命名 `QuickDeliverException`。增强 Hook 直接调用自有方法守卫，源码 JNI／反射／MethodHandle 解码后转发守卫回调；对应两个源码核心导出已删除。字段槽计数也直接读取增强注册表，删除旧计数桥和未使用的源码汇总导出。类型化解码、接收者与 final 检查仍需迁移。
