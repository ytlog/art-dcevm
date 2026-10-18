# 能力限制

[English](LIMITATIONS.en.md) | 中文

本文汇总使用时需要知道的前提、会被拒绝的变化以及各类变化的语义。实现机制见 [STRUCTURAL_REDEFINE_ENGINE.md](STRUCTURAL_REDEFINE_ENGINE.md)。

## 提交与错误处理

初始化成功后，直接通过调用方 Agent 的标准 `jvmtiEnv::RedefineClasses` 提交完整批次。返回 `0`（`JVMTI_ERROR_NONE`）表示成功；任何非零返回值表示失败，读取该错误码及 `DcevmRuntime.capabilities()` 中的 `lastRejection`，旧定义保持不变。`lastRejection` 描述最近一次标准入口事务的失败原因，成功时清空。

`capabilities()` 只用于诊断，不作为提交前的准入判断；不要用其中的布尔值或状态字符串拦截提交。准入与支持的变化由实际事务检查决定。诊断字段参考见[测试指南](TESTING.md#诊断报告参考)。

## 前提限制

- 应用必须从进程启动时就是 Java 可调试的；release 进程中 ART 不开放 `can_redefine_classes`，本库无法工作。
- 仅支持 arm64-v8a 的 Android 11–17。
- 运行库缺少基础入口的任一命名符号，或公开 JVMTI 表与找到的命名包装器不一致时初始化失败。方法与字段退役另需各自的名称；缺少时只关闭相应能力，并在 `capabilities()` 的诊断中说明。
- 退役依赖运行时探测确认 ART 的类布局声明；探测不通过的事务不允许退役，`capabilities()` 的 `memberMapping` 给出原因。
- 增强范围是标准 `RedefineClasses`。标准入口的批次读取和事务执行已串行化；直接调用 ART 结构重定义扩展的并发事务不参与该保护。

成员退役还要求 Native 阶段的线程与命名读屏障服务检查通过；线程或类布局检查失败时按原有错误规则拒绝。设备覆盖及尚待验证的版本见[兼容性结论](COMPATIBILITY.md)。

## 接口重定义

接口目标支持 default、static、private 方法体修改，新增 public abstract、default、static（任意访问级别，含 synthetic）、private 实例方法，删除或改签名任一方法（含 default/abstract），已有 public 实例方法在 abstract 与 default 之间切换，方法在 default/abstract、static、private 之间改变调用方式（规则同普通类，见下表 71），新增、删除或改类型静态字段，以及新增或移除父接口。顶层与嵌套接口可与实现类、实现类子类或无关类同批，也可提交多个接口和父子接口；任一目标失败时整批保持旧定义。lambda 等新合成类须由热重载端先追加到加载器，发生变化的已有合成类须在同批重定义。

此能力在 Android 11–17（API 30–37）的 arm64 设备上可用。API 33–36 已经真机验证；API 30–32 与 37 依固定 AOSP 声明及这些版本全部已存设备运行库中的命名服务启用，尚未在设备上运行接口套件，探测通过后 `interfaceRuntime` 会注明这一点。运行时声明仍须通过探测，版本号本身不保证可用。

| 接口目标的拒绝情况 | JVMTI 错误码 |
| --- | --- |
| 新增 native；direct 方法遮蔽继承的接口／Object 方法；选择服务不可用时 default 与非 public 类方法同签名或旧 ART 无法链接非 public 实现 | `METHOD_ADDED`（63） |
| 成员映射不可用时删除或改签名方法 | `METHOD_DELETED`（67） |
| 字段修饰符变化；成员映射不可用时的字段增删或改类型 | `SCHEMA_CHANGED`（64） |
| 接口方法 abstract 与 default 互换及调用方式变化之外的修饰符变化；选择服务不可用时改为 default 后出现上述同签名 | `METHOD_MODIFIERS_CHANGED`（71） |
| 成员映射或分派封闭不可用时移除父接口；成员映射不可用时新增父接口；新父接口的方法被 static／private 方法遮蔽，或选择服务不可用且其 default 出现上述同签名 | `HIERARCHY_CHANGED`（66） |
| 新增的接口由同批重建，而新类与该重建互相依赖，或同一重建组内新类排在它之前 | `HIERARCHY_CHANGED`（66） |
| class/interface 跨界时存在已加载子类型；需要实例前置检查却能力不可用或存在直接实例 | 分别为 62、70；允许的种类转换见[转换矩阵](CLASS_KIND_EVOLUTION.md) |
| 接口重建组之外的实现类父类在同批改变父类 | `HIERARCHY_CHANGED`（66） |
| boot 接口、当前使用指针型 JNI ID，或目标／任一子类型带 pointer-id 标记 | `UNMODIFIABLE_CLASS`（79） |
| 运行时版本、必需命名服务或声明探测不可用 | `NOT_AVAILABLE`（98） |

删除的 default/abstract 方法与普通类的退役方法相同：经接口或实现类的字节码调用、缓存的反射、JNI 与 MethodHandle 抛 `NoSuchMethodError`，实现类自身声明的同名方法保留；改签名即旧签名退役、新签名新增。default 改为 abstract 与新增 abstract 相同，未实现它的类调用时抛 `AbstractMethodError`；abstract 改为 default 后，未声明实现的实现类获得 copied default。接口在重定义前已初始化，新增的静态字段取默认值；与普通类相同，新增或修改的 `<clinit>` 不会在已初始化的接口上执行；常量表达式通常已被调用方内联。新增父接口时，新父接口及其目标原先未继承的父接口中的 abstract 方法与新增 abstract 相同；其 default 由实现类获得 copied default。

`lastRejection` 给出被拒类名和原因；失败不改变旧行为。 新 default 对已有实例立即生效，没有提交 DEX 的实现类也获得 copied default；类链 public 实现与更具体的子接口仍优先。static/private 退役沿用普通类的字节码、JNI、反射与 MethodHandle 语义。与 JBR 相同，新出现的 default 冲突照常接受，经冲突方法的调用抛 `IncompatibleClassChangeError`，之后消除冲突的事务恢复唯一的 default。插入 lambda 可能改变合成类序号，热重载端须像处理普通类一样隔离旧闭包或保留旧桥，见[接口成员演进](STRUCTURAL_REDEFINE_ENGINE.md#接口成员演进)。

接口事务要求 ART 可以改写已有成员的 JNI ID。纯 pointer 模式可能没有 ClassExt 的 pointer-id 标记，因此在 Native 快照阶段还检查公开 JVMTI 返回的方法或字段 ID 表示；指针 ID 在进入 ART 事务前以 79 拒绝。不能仅切换模式来修复此前缓存的指针 ID。普通类仍按其原有规则验收。

与 JBR 相同，新增 public abstract 方法不要求已加载的实现类提供实现：已有实现、继承实现、同批补齐的实现与 ART 选中的更具体 default 照常分派；缺少实现、缺少真实 bridge 或被更具体 abstract 遮住的 default，在调用时抛 `AbstractMethodError`。`interfaceMethodSelection=true` 时，package-private/protected 类虚方法（含另一运行时包的父类方法）在接口选择中优先于 default，事务成功，接口调用抛 `IllegalAccessError`；原有类方法调用保留原行为。private 方法本身不实现新增 abstract，调用抛 `AbstractMethodError`。

新增 default、abstract/default 互换与新增父接口带来的方法共用同一选择路径。private/static 声明不作为接口实现；更具体的 public 类覆写照常优先。private/default 的结果取决于接口进入类链的位置：该类首次实现接口时可以选择 default；类链有 private、且从父类继承接口 default 时选择 `AbstractMethodError`，包括自身重声明 private 与后加载子类。详见[选择矩阵](STRUCTURAL_REDEFINE_ENGINE.md)。命名链接、分配与错误服务缺失时 `interfaceMethodSelection=false`，保留原有保守拒绝；平台验证记录见[兼容性说明](COMPATIBILITY.md)。

多个接口组共同提交。快照后加载的相关类型触发现有有界重试，达到上限时整批拒绝 113，撤回或修正后可重新提交。失败不改写旧定义、已缓存的反射／MethodHandle／JNI method ID 或预热调用点。

子类型相交的接口组，以及同批做结构修改、又是组内类型父类的组外类，合并为一次重建，每个类型只替换和迁移一次，与提交顺序无关。ART 的实例收集只覆盖拥有者条目所属类的实例；没有一个根覆盖全部具体成员时，引擎让 ART 以其余各根依次再收集一轮，每个实例只保留在它最先可赋值的根那一轮，因此无关接口共享实现类、接口与组外父类各有独立实例时同样只迁移一次。组外父类只改方法体时不并入重建，仍原地更新。

接口方法体修改也会重新分配所有已加载实现类及其子类的实例，保留兼容字段值，改写引用并去优化；代价随受影响实例与堆大小增加。它不会重新执行已有实例的构造器。

`java.lang.reflect.Proxy` 生成的实现类（例如 Retrofit 服务或注解实例）保留原类与 `InvocationHandler`：保留的方法照常交给处理器，处理器收到的是新接口的 `Method`；Proxy 没有的新增 abstract 方法调用时抛 `AbstractMethodError`，新增 default 执行其方法体；之后用同一接口创建的代理复用这个类，行为相同，与 JBR 一致。接口新增父接口时，已有和之后创建的代理都成为该父接口的实例，与 JBR 一致：Proxy 原本没有的父接口方法执行父接口的 default 方法体或抛 `AbstractMethodError`，Proxy 原本就有的方法仍交给处理器。新增 abstract 方法不因 Proxy 缺实现而拒绝（Proxy 没有可以同批补齐的源码），普通实现类同样在调用时按上文规则抛错。父接口按整批的最终关系统一更新：支持移除与恢复、多个接口共同新增父接口，以及新父接口同批重定义；还有其他继承路径的父接口保留，最后一条路径移除后代理才失去该类型。

## 不支持的变化（以 JVMTI 错误拒绝，旧定义保持不变）

| 变化 | 错误码 | 说明 |
| --- | --- | --- |
| 虚方法改为 private，或已删除的虚方法以 private 方式重新加入，而检测到批次类的活动帧且缺少自身调用选择能力 | `UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED`（71） | JBR 可让自身旧帧直接调用新 private 方法。导出命名 `FindMethodToCall<kVirtual/kInterface>` 选择入口的运行时在 Class/Thread/HandleScope 探测通过后支持此行为，以 `privateSelfSelection` 报告：API 36（已经真机验证）与多数 API 34–35 运行库（依固定源码，尚未上机）。API 30–33 以及部分 API 34、37 模块内联了选择逻辑，仍拒绝 71。无访问权的批次外旧调用方可保留，调用时抛 IAE，见[调用方式变化](STRUCTURAL_REDEFINE_ENGINE.md#调用方式变化) |
| 种类变化不满足实例、子类型、分配或分派前提 | 62／66／70 | 见[类种类演进](CLASS_KIND_EVOLUTION.md) |
| 失去原有父类的父类变化 | `UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED`（66） | 已校验代码依赖旧类型关系，见引擎文档[收窄不支持](STRUCTURAL_REDEFINE_ENGINE.md#收窄不支持) |
| 移除接口时成员映射或分派保护不可用 | `UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED`（66） | 见[移除接口的语义](#移除接口的语义) |

方法删除/改签名与字段删除/改类型/存储变化在退役不可用时分别以 `UNSUPPORTED_REDEFINITION_METHOD_DELETED`（67）与 `UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED`（64）拒绝，native 改 Java 以 71 拒绝；新增接口与父类变化在没有探测通过的类布局时以 66 拒绝；父类变化还需要校验重定向。

## 语义冲突（会被拒绝）

- 方法新增 final，而已加载的子类覆写了它。
- 类新增 final，而已有已加载的子类（70）。
- 类新增 abstract，而该类自身仍有实例（70）；子类的实例不受影响。
- 新增的接口无法经目标类的加载器解析、不是接口，或对目标不可访问（`JVMTI_ERROR_FAILS_VERIFICATION`，62）。
- 新父类无法解析、是接口、是 final、不可访问，或新定义与已加载子类覆写了它的 final 方法（62）；新父类扩展了该类本身（`JVMTI_ERROR_CIRCULAR_CLASS_DEFINITION`，61）；新父类不扩展原父类（66，收窄）；同一批次还结构性地重定义了该类的父类型或新父类链上的接口，或同样改变父类的子类，或新父类链上结构变化的类也改父类（66）；这些父类型只改方法体时可以同批，新父类链上原父类之下的普通类可以结构变化，只改成员的子类也可以同批，并按新的父类链校验。
- 退役不可用时，新增或改变访问级别的方法可能顶替已加载子类的接口 default/miranda 拷贝。

## 父类变化的语义

- 只支持放宽：新父类必须扩展原父类。已有实例保留自身与原父类链的字段值，新父类的字段为零值，不执行新父类的构造器；新父类在重定义前加载但不初始化。
- 虚方法按新层级分派，新父类覆写的方法对已有实例立即生效；已在执行的旧方法帧中的 `super` 调用仍按旧层级。
- ART 13 起校验新类时按名称解析到旧类，引擎在校验期间改为返回新类。若某版本的校验器走其他解析路径，重定义以 62 拒绝，旧定义保持不变。

## 类修饰符的语义

- 可见性、final、abstract、synthetic 与嵌套类的 InnerClass 标志可以改变，新类使用新修饰符。已解析的调用点与类型引用保留，ART 不对它们重新做访问检查；新的解析按新修饰符检查。

## 新增接口的语义

- 已有实例与已加载子类的实例随即成为新接口的实例。新增的接口在重定义前经目标类的加载器加载但不初始化，其静态初始化照常在首次需要时执行；该加载器随之成为这个接口的发起加载器。


## 移除接口的语义

- 普通类可以不再实现某个接口。已有实例与已加载子类的实例随即不再是该接口的实例（`instanceof`、`isInstance` 与强制转换按新层级判断）。
- 实例字段按 JBR 的持有者与实例更新规则清理不兼容引用；静态字段另按 JBR FieldCopier 的重定义类与签名规则复制，见[引擎文档](STRUCTURAL_REDEFINE_ENGINE.md#移除接口)。已有 `Thread` 持有的任务可能被清空，此后 `run()` 无动作；恢复接口也不会恢复被清空的字段。数组元素、局部变量和收窄后才创建的引用不受该次字段清理影响；经保留引用调用不再实现的接口时抛 `IncompatibleClassChangeError`，解释执行、JIT 与 boot 调用都须遵循此结果。
- 类从被移除接口继承的 default 方法随之消失：以类本身调用它的旧字节码抛 `NoSuchMethodError`。
- 首次接口收窄后，分派保护持续到进程结束，覆盖后续结构重建与新链接的类，包括抽象父类的晚加载具体子类。接口调用需要查找；全进程后续链接与字段快照的性能成本尚未量化。
- 接口目标也可以移除父接口：其子接口、实现类及其子类随之失去该父接口，语义同上；Proxy 实现类按整批最终接口图移除该父接口，与 JBR 一致。
## 方法种类的语义

- 删除的 native 方法与其他方法一样退役；正在执行它的线程继续执行。native 改为 Java 后，已有的调用点、缓存的反射对象与 JNI 方法 ID 执行新的 Java 方法体。
- 方法变为 abstract 后，没有实现它的子类实例被调用时抛 `AbstractMethodError`。
- 删除的构造器：字节码 `new`、JNI `NewObject*` 抛 `NoSuchMethodError`，缓存的 `Constructor.newInstance` 抛包装它的 `InvocationTargetException`。
- 删除类初始化器不影响已初始化的类，静态字段保留原值。

## 退役成员的语义

- 删除的方法和签名替换前的方法退役：字节码、JNI、MethodHandle 再调用时抛 `NoSuchMethodError`，缓存的反射 `Method.invoke` 抛包装它的 `InvocationTargetException`；已在执行的旧方法帧继续执行旧方法体。
- 退役字段：字节码、JNI、MethodHandle 访问抛 `NoSuchFieldError`；缓存的反射 `Field` 访问实例字段抛 `IllegalArgumentException`，访问静态字段抛 `NoClassDefFoundError`；VarHandle 抛 `UnsupportedOperationException`。缓存的反射 `Field` 的名称与声明类会显示为库内占位类的字段。
- 恢复同名成员会产生新成员，旧的缓存引用保持退役。恢复时 static／实例方式互换的旧调用按调用方式保护抛 ICCE；原虚方法恢复为 private 后，无访问权的旧外部调用抛 IAE；自身旧活动帧仍不支持。
- 发生过退役的类及其 class loader 保留到进程结束。

## ART 自身的限制

- 普通类结构重定义沿用 ART 的检查：数组、基本类型、代理类、`java.lang.String`、mirror 类，以及 `java.lang.Thread` 本身和它的父类型不可重定义；应用自己的 `Thread` 子类可以重定义，已启动线程的对象身份、名称、ID、中断与 join 保持不变，正在执行的 `run()` 帧继续执行旧方法体；在本线程类初始化过程中不能重定义该类；存在指针形式 JNI ID 的类不能做结构变化。
- `sun.misc.Unsafe`、`Atomic*FieldUpdater` 等缓存的原始字段 offset 不经过 ART 的任何入口；任何改变实例布局的结构重定义（包括新增字段）都会使其失效。

## 进程内的副作用

- 方法退役启用时，库通过 JVMTI `SetJNIFunctionTable` 包装 JNI 调用与字段访问；ART 设置覆盖表后会关闭本进程的 Core Platform API 检查，该检查只约束平台模块的调用方。
- 每次经标准入口重定义前，库通过目标类的定义加载器加载（不初始化）新定义引用的全部类，防止 ART 在事务中途才加载子类、使其留在旧类之下（见 [结构重定义引擎](STRUCTURAL_REDEFINE_ENGINE.md#事务中的类加载)）。事务期间库启用 JVMTI `ClassPrepare` 事件。若仍有目标类的子类在事务中加载（例如 ClassFileLoadHook 改写后才引用它，或其他线程并发加载），该次尝试会在提交前失败并自动重试，RedefineClasses 会再次调用 ClassFileLoadHook，RetransformClasses 仅用相同最终字节重试 Direct；连续三次失败时返回 `JVMTI_ERROR_INTERNAL`。

## 提供新类

`RedefineClasses` 只替换已加载的类。补丁中 APK 没有的类（新类，以及新 lambda 等合成类）由工具经类加载器提供，本库不定义新类，这与 Java 上的 DCEVM 和热重载工具的分工相同。工具对一次更新按以下顺序处理：

1. 每个类各编成一个 DEX（D8 `--file-per-class` 会把新 lambda 的合成类单独输出）。
2. 用 `Class.forName(name, false, 应用类加载器)` 分类：找得到的是已有类，尚未使用过的已有类也会因此加载，之后才能重定义；找不到的是新类。
3. 把新类打成一个 jar 并设为只读，再用 ART 的 JVMTI 扩展 `com.android.art.classloader.add_to_dex_class_loader` 把它追加到应用类加载器（`BaseDexClassLoader`）的路径末尾。Android 14 起，目标 SDK 为 34 及以上的应用不能加载可写的 DEX 文件。
4. 用一次 `RedefineClasses` 提交全部已有类。新定义引用的追加类，包括被重定义类的新子类，由本库在事务开始前加载。

追加只能加在加载器路径末尾，无法撤销，也不会覆盖已加载的类；追加过的类再次修改时，按已有类重定义。新类的静态初始化器在首次使用时执行，被重定义类的静态初始化器不会重新执行。示例实现见 [HotReloadTool.java](../app/src/debug/java/com/github/ytlog/artdcevm/testing/HotReloadTool.java)。

## 语义说明

- 已有对象的新增字段取零值或 `null`，不重新执行构造器；新建对象执行新构造器。
- 结构变化会让 ART 遍历堆迁移实例、去优化所有线程帧并清空 JIT 代码，耗时随堆大小增加，提交后需要重新预热。
- 补丁分发、签名校验和访问控制由调用方负责。arm64 Android 13–17 的 `RetransformClasses` 将 ClassFileLoadHook 最终 DEX 送入增强结构路径，使用相同的新增与退役规则；Android 11/12 保持 ART 原生规则。需两个命名服务、匹配的公开函数表和固定声明校验通过；不通过则回退 ART 原生规则。路由及其重试不重复调用 ClassFileLoadHook，`retransformRouted` 报告安装能力。

## 与 JBR DCEVM 的对照

目标语义以固定 JBR DCEVM 源码和对应运行时对照为准（`-XX:+AllowEnhancedClassRedefinition`）。下表记录当前差异及关键边界，不是完整功能已对齐的声明。当前以错误码拒绝某项变化，不代表已经满足对齐目标；尚有差异的路径仍需修复。

| 变化 | JBR DCEVM | art-dcevm |
| --- | --- | --- |
| 持有动态代理的实例字段改类型 | 新类型是直接声明接口或 `Proxy` 父类时保留；仅间接实现接口时清空 | 目标与继承字段按同一规则处理，见[字段改型对照](TESTING.md#jbr-对照) |
| 接口方法与非 public 类方法同签名 | 接受；private 不实现 abstract，抛 `AbstractMethodError`；package-private/protected 抛 `IllegalAccessError` | 选择服务可用时相同；类方法调用保留，支持 default/abstract 切换与新增父接口；API 33/36 已验证 |
| 普通类方法在 static 与实例之间切换，而批次外已加载代码仍按旧方式调用 | 接受，调用时重新解析并抛 `IncompatibleClassChangeError` | 命名调用方式保护可用时接受，旧调用抛 ICCE；旧调用方禁止 JIT 编译及内联，ART 仍执行原有去优化。Android 13、16 覆盖普通／CheckJNI、热调用、活动帧、GC 与隐式重建；入口不可用时保留 71 |
| 普通类虚方法改为 private，批次外仍有无访问权的旧调用方 | 接受；旧 virtual/super 调用抛 `IllegalAccessError`，自身旧帧可调用新 private 方法 | 接受并保护外部旧调用，抛 IAE；旧调用方禁止 JIT 编译及内联。有命名选择服务时（`privateSelfSelection`：API 36 已经真机验证，多数 API 34–35 运行时依源码对齐），自身旧帧直接选择 private 方法并忽略覆写，含接口 default 副本；选择逻辑被内联（API 30–33）或运行时探测不匹配时拒绝 71 |
| 父类收窄（新父类链不含原父类） | 拒绝 | 拒绝 66 |
| 失去接口的抽象类之后才加载的具体子类 | 按新层级正确分派 | 持久发布前保护与后续结构重建共同保留正确分派，见[引擎文档](STRUCTURAL_REDEFINE_ENGINE.md#移除接口) |
| 有 Proxy 实现类的接口移除父接口 | 接受，已有代理不再是该父接口的实例 | 接受；共享父接口保留到最后一条继承路径被移除，已有引用沿用接口收窄规则 |
| 类种类变化 | 按方向和已有对象／子类型判断；部分成功返回后的缓存／GC 行为不安全 | 按[转换矩阵](CLASS_KIND_EVOLUTION.md)放行可验证的转换，保留必要的 62／66／70 边界 |
| 类有自身实例时改为 abstract | 接受，保留兼容字段与实例，缺实现调用抛 `AbstractMethodError`，禁止正常构造；有固定解释器/JIT/JNI 对照 | 仍拒绝 70：抽象类的独立 vtable／静态字段布局与 JIT/nterp 的嵌入分派假设冲突；GC 也按可实例化状态计算静态引用起点，见[阻塞分析与源码设计](LIVE_ABSTRACT_EVOLUTION.md) |
| boot 类、`Thread` 及其父类型、数组、代理类自身 | 部分可重定义 | 按 ART 的规则拒绝 |
| 新类（新 lambda、匿名类） | 由 HotswapAgent 等工具定义 | 由热重载工具追加到加载器，见[提供新类](#提供新类) |

接口收窄字段清理、静态复制及保留的 JNI／MethodHandle 调用路径各有独立规则与验证范围，见[引擎文档](STRUCTURAL_REDEFINE_ENGINE.md#移除接口)。DEX 丢失原 ClassFile 字段声明顺序，纯声明重排的实例更新语义也尚未证明等价。
