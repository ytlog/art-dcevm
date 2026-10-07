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

## 目标设备符号核对（2026-10-07）

在 Android 13 真机 `DEVICE_001` 上读取 `/apex/com.android.art/lib64/` 的设备库，Build ID 分别为 `libopenjdkjvmti.so` 的 `2fe21c45581b9c8b0519b71cbde32938` 和 `libart.so` 的 `14fc77b24db0b351b919c97009dc86bf`。用 NDK `llvm-readelf --dyn-syms --wide` 核对动态符号表：

- `ClassRedefinition::RecordNewFieldAdded`、`RecordNewMethodAdded`、`CheckFields`、`CheckMethods`、`CollectNewFieldAndMethodMappings` 与 `UpdateClassStructurally` 均有命名导出。前两个可供自有校验代理报告结构变化；名称存在仍不能代替事务对象布局探针。
- `libart.so` 导出 `jit::JitCodeCache::InvalidateAllCompiledCode` 和 `LinearAlloc::ContainsUnsafe`。取得接收者对象仍需固定源码声明与运行时校验，不能从二进制推算成员偏移。
- `CopyFields` 没有独立动态导出。字段删除和类型变化不能通过只 Hook `CheckFields` 来实现：设备提交仍按旧字段都存在于新布局的前提复制；必须在自有 `UpdateClassStructurally` 代理中处理字段匹配、默认值和回滚边界，并保持旧引用失效语义。

设备库副本仅保存在已忽略的 `build/evidence/device-symbols/20261007/`，不作为构建输入或提交内容。
