# 有存活实例的具体类转 abstract

当前状态：**尚未在设备 ART 上开放**，原有“改为 abstract 且存在直接实例时返回 70”的边界保留。这里记录固定 JBR 的可复现实测契约和当前 ART 布局阻塞，不能把 JBR 探针通过解释为增强库已经支持。

## 固定 JBR 契约

参照为 JBR 21.0.11+10-b1163.116，源码修订 `cf1f197170a0f04c6559feb873e362583be021d1`。运行：

```sh
python3 tools/compare-jbr-live-abstract.py --java-home <fixed-jbr>
```

工具分别以 `-Xint` 和 `-Xbatch` 运行同一组 classfile 与 JNI 库；JIT 模式限定编译热 virtual/interface 调用方及活动帧调用方，并要求每轮转换后它们实际重编译的记录，主执行器仍由解释器执行。原始输出、classfile/JNI 库哈希及逐次结果只写入已忽略的 `build/evidence/`。

探针把已有直接实例和已有子类实例的具体类改为 abstract，同时将一个已有具体方法改为 abstract，保留其他方法和字段声明；每个模式执行两轮转换和恢复。需要验证的契约为：

| 行为 | 固定 JBR 的结果 |
| --- | --- |
| 当前类身份、旧对象与自引用、数组中的引用 | 保留；旧实例的 `getClass()` 等于当前类字面量，instanceof/checkcast 继续成立 |
| 兼容实例字段 | int、long、Object 值保留；缓存 Field 可读，引用在 GC 后保活 |
| 兼容静态字段 | int、Object 值保留；仅由静态字段强持有的引用在 GC 后保活 |
| 仍有实现的方法 | 旧字节码、缓存反射、缓存 MethodHandle 和 JNI ID 均调用新方法体 |
| 缺少实现的方法 | 旧 virtual/interface 调用、热调用、缓存与新建反射/MethodHandle、JNI virtual/interface/nonvirtual 调用均抛精确 AbstractMethodError；反射外层保留 InvocationTargetException |
| 已有子类型 | 更具体覆写继续优先；原来继承该具体方法而未覆写的子类，在更新后调用该方法抛 AME |
| 更新后的普通 `new` | InstantiationError |
| JNI NewObject、JNI AllocObject、构造 MethodHandle | InstantiationException |
| 活动旧帧 | 旧帧继续执行；恢复后发出的 virtual 调用选择当前方法，缺实现时抛 AME |
| 恢复为具体类 | 原有对象与字段继续可用，缺实现的方法恢复，普通构造、JNI 分配和缓存构造 MethodHandle 恢复可用 |
| 失败批次 | 同批另一个输入损坏时，固定 JBR 抛精确 InternalError；类状态、方法结果和无关目标保持不变 |

工具单独记录反射缓存的历史行为，避免把它们与当前运行时类混为一谈：已预热的 Constructor 及共享其访问器的副本在 abstract 状态包装 InstantiationException，恢复后可再次构造；首次在 abstract 状态使用的 Constructor 直接抛 InstantiationException，恢复后仍缓存该失败。探针的缓存 static final Class 引用还可保留旧修饰符，而当前类字面量和旧实例的 `getClass()` 已报告 abstract；缓存 Method 的声明类引用则更新到当前类。这些结果需要分别保留，不能只检查一次 `getModifiers()`。

## ART 的布局冲突

以下判断来自固定 ART 源码声明及实际调用路径，不依赖反汇编或推算偏移：

1. `Class::ShouldHaveEmbeddedVTable()` 和 `ShouldHaveImt()` 依据 `IsInstantiable()`，普通 abstract 类返回 false。它们的 vtable 保留为独立 PointerArray；具体类则把 vtable 长度、IMT 指针和 vtable 项放在 Class 头部之后。见 [class-inl.h](https://android.googlesource.com/platform/art/+/refs/tags/android-13.0.0_r1/runtime/mirror/class-inl.h#307)。
2. `ClassLinker::LinkClass` / `LinkStaticFields` 使用上述判定决定 Class 大小、静态字段起点、是否分配 IMT、是否由 `Class::CopyOf` 扩容并填充嵌入表。普通 abstract 类的静态字段从 Class 头部末尾开始。见 [LinkClass](https://android.googlesource.com/platform/art/+/refs/tags/android-13.0.0_r1/runtime/class_linker.cc#5856) 和 [静态引用字段起点](https://android.googlesource.com/platform/art/+/refs/tags/android-13.0.0_r1/runtime/mirror/class-inl.h#711)。
3. GC 扫描静态引用时根据 Class 的 abstract/可实例化状态重新计算起点，并不是逐个读取 ArtField.offset。因此，在 abstract 布局后追加分派表或按具体类布局分配后改 abstract 标志，会让 GC、静态字段和分派表对同一地址产生不同解释。见 [Object::VisitFieldsReferences](https://android.googlesource.com/platform/art/+/refs/tags/android-13.0.0_r1/runtime/mirror/object-inl.h#867)。
4. arm64 JIT virtual 调用直接读取 `EmbeddedVTableEntryOffset`；interface 调用直接读取 `ImtPtrOffset`。nterp 的对应快路径也直接读取固定嵌入表。它们不经过可以统一替换的 `GetVTableEntry` 调用。见 [JIT ClassTableGet](https://android.googlesource.com/platform/art/+/refs/tags/android-13.0.0_r1/compiler/optimizing/code_generator_arm64.cc#6889) 和 [nterp invoke](https://android.googlesource.com/platform/art/+/refs/tags/android-13.0.0_r1/runtime/interpreter/mterp/arm64ng/invoke.S#69)。
5. JNI、反射、MethodHandle 和 switch 解释器还存在经 `FindVirtualMethodForVirtualOrInterface` / `GetVTableEntry` 选择独立 vtable 的路径；MethodHandle 的接收者等于声明类时还可能直接使用目标方法。这些路径与 JIT/nterp 的固定地址快路径必须同时正确，单一路径通过不足以开放。
6. 原事务 `CollectAndCreateNewInstances` 调用 `Class::Alloc` 为每个旧实例分配替换对象，其声明要求目标可实例化。现有实例检查位于该原函数之后；直接移除检查不会为 abstract 类补出嵌入分派布局，也没有改变 ART 分配契约。

固定 ART 11–17 的 Class 声明和上述核心布局判定一致。已收集运行库中某些布局 helper 虽保留命名符号，其调用点仍可内联；符号存在并不证明 Hook 可以覆盖 JIT、nterp 和 GC 的全部直接访问。

## 需要的 ART 原生决策

完整实现需要把“允许创建新的应用实例”与“已有实例需要嵌入分派布局”分开。一个可供源码实现评审的设计是：

- 在替换 Class 的 Setup/Link 之前，由 ART 事务设置独立的布局属性；普通 abstract 语义位始终保留，布局属性只允许非接口的演进目标，并随后续显式及隐式结构重建保留。接口重建组的实例收集拥有者选择也需考虑这些可能有直接实例的 abstract 类型。
- `ShouldHaveEmbeddedVTable`、`ShouldHaveImt`、类大小、静态字段起点、GC 静态引用扫描及子类链接中的 superclass vtable/IMT 选择统一使用该布局属性。静态字段在首次链接时就位于最终正确的位置。
- 原 ART 事务的实例分配提供专用入口，允许迁移已有 abstract 实例；正常 new、反射、JNI 分配和构造 MethodHandle 继续依据 abstract 语义拒绝。实例迁移、引用改写、去优化和缓存失效仍由原事务完成。
- 审计 JIT 中“abstract 不会有直接实例”的优化假设，在定制 ART 上验证同一对象的解释器/JIT/nterp、JNI、反射和 MethodHandle 分派，再验证子类、恢复、GC、活动帧、后续无关结构重建以及分配后失败回滚。不能以 JBR 探针替代这些设备验证。

这要求修改并重建 ART 的内联布局判断及其使用者。仓库当前约束为通过命名 ELF Hook 和固定声明使用设备 ART 的决策点；在这一约束下，尚未找到可证明覆盖全部访问路径的实现。本次没有绕过实例检查，也没有修改已发布 Class 的 abstract 标志。
