# 能力限制

[English](LIMITATIONS.en.md) | 中文

本文汇总使用时需要知道的前提、会被拒绝的变化以及各类变化的语义。实现机制见 [STRUCTURAL_REDEFINE_ENGINE.md](STRUCTURAL_REDEFINE_ENGINE.md)。

## 前提限制

- 应用必须从进程启动时就是 Java 可调试的；release 进程中 ART 不开放 `can_redefine_classes`，本库无法工作。
- 仅支持 arm64-v8a 的 Android 11–17。
- 运行库缺少基础入口的任一命名符号，或公开 JVMTI 表与找到的命名包装器不一致时初始化失败。方法与字段退役另需各自的名称；缺少时只关闭相应能力，并在 `capabilities()` 的诊断中说明。
- 退役依赖运行时探测确认 ART 的类布局声明；探测不通过的事务不允许退役，`capabilities()` 的 `memberMapping` 给出原因。
- 增强范围是标准 `RedefineClasses`。标准入口的批次读取和事务执行已串行化；直接调用 ART 结构重定义扩展的并发事务不参与该保护。

普通类的成员退役也依赖 Native 阶段对当前 ART Thread 声明与命名读屏障服务的探测。因此 `memberMapping` 的拒绝原因可能来自线程探测，也可能来自 Class 布局探测；映射不可用时保留原始安装原因。Thread 探测已在 Android 13/API 33 和 Android 16/API 36 上做设备验证。Android 14/API 34 的 CMC 配置已通过普通与 CheckJNI 的完整普通类场景及 Thread 拒绝／GC 压力检查，CC 配置仍待验证；该结果未开放接口重定义。Android 11、12、15、17 只有源码声明与主机核对，尚无这项探测的设备验证。

其他版本先通过 `tools/run-device-tests.py --scenario-group ordinary` 的完整普通类场景，以及 `InterfaceAbortInstrumentation` 的 `-e scenario ordinary` Thread 拒绝探测和 GC 压力检查；两者均须覆盖普通与 CheckJNI 模式。该模式保留普通类新增接口的场景，只跳过以接口为重定义目标的测试，不需要先开放接口准入。Android 14 及以上还须分别确认 CC 和 CMC 收集器的实际配置与运行结果；GC 次数或零次标记期间读取不能单独证明 CMC 覆盖。

## 接口重定义

接口目标支持 default、static、private 方法体修改，新增 public abstract、default、static（任意访问级别，含 synthetic）、private 实例方法，以及删除或改签名 static/private 方法。顶层与嵌套接口可与实现类、实现类子类或无关类同批，也可提交多个接口和父子接口；任一目标失败时整批保持旧定义。lambda 等新合成类须由热重载端先追加到加载器，发生变化的已有合成类须在同批重定义。

此能力仅在 Android 13（API 33）和 Android 16（API 36）的 arm64 设备上可用；其他版本停用并在 `capabilities()` 中说明原因。运行时声明仍须通过探测，版本号本身不保证可用。

`capabilities()` 只读取最近的探测状态，不触发探测。`interfaceRuntime` 的 `unavailable` 表示接口运行时不可用；`disabled: 原因` 表示已停用，例如版本、必需服务或运行时声明不匹配；这两种状态下接口目标以 98 拒绝。`unverified: …` 表示事务内探测尚未完成，`verified: …` 表示探测已通过。初始化后、首次接口事务完成运行时探测前，`interfaceRedefinition` 为 false；`unverified` 不等于不支持，不应据此拦截首次事务。探测通过也不保证每种修改都被接受，最终以具体事务结果和 `lastRejection` 为准。

| 接口目标的拒绝情况 | JVMTI 错误码 |
| --- | --- |
| 新增 native、`<clinit>`；新增 abstract 无有效实现或 IfTable 探测不通过；direct 方法遮蔽继承的接口／Object 方法；default 遮蔽类链 static／非 public 方法或产生 default 冲突 | `METHOD_ADDED`（63） |
| 删除或改签名 default/abstract 方法 | `METHOD_DELETED`（67） |
| 字段变化 | `SCHEMA_CHANGED`（64） |
| 方法修饰符变化 | `METHOD_MODIFIERS_CHANGED`（71） |
| 父接口变化 | `HIERARCHY_CHANGED`（66） |
| 类修饰符变化 | `CLASS_MODIFIERS_CHANGED`（70） |
| 两个无关接口共享实现类或其他子类型，且批次中没有共同接口祖先 | `HIERARCHY_CHANGED`（66） |
| 接口重建组之外的实现类父类在同批做结构修改 | `HIERARCHY_CHANGED`（66）；仅改方法体可接受 |
| boot 接口、有 Proxy 实现类，或目标／任一子类型带 pointer-id 标记 | `UNMODIFIABLE_CLASS`（79） |
| 运行时版本、必需命名服务或声明探测不可用 | `NOT_AVAILABLE`（98） |

`lastRejection` 给出被拒类名和原因；失败不改变旧行为。 新 default 对已有实例立即生效，没有提交 DEX 的实现类也获得 copied default；类链 public 实现与更具体的子接口仍优先。static/private 退役沿用普通类的字节码、JNI、反射与 MethodHandle 语义，default 冲突原因写明实现类和方法。插入 lambda 可能改变合成类序号，热重载端须像处理普通类一样隔离旧闭包或保留旧桥，见[接口成员演进](STRUCTURAL_REDEFINE_ENGINE.md#接口成员演进)。

新增 public abstract 方法要求重定义后，接口组内**每个已加载的具体实现类**都能通过 ART 的实际 IfTable 分派到同名、完整 descriptor 相同的 public 具体实例方法，包括没有实例、没有提交 DEX 的类。同批补齐实现不要求提交顺序；已有实现、父类继承的实现和 ART 实际选中的更具体 default 均可接受。缺少实现、abstract/miranda、static/private/非 public 方法或 default 冲突均整批拒绝 63，不推迟到调用时抛 `AbstractMethodError`。泛型与协变返回依赖真实 bridge，不把相同名称或不同返回类型当作实现；同名类型按类加载器身份分别判断。`lastRejection` 写出接口方法及缺实现的类，撤回或补齐后可重试。

IfTable 只读访问器须通过 Class 字段、接口方法索引和 arm64 PointerArray 探测。`interfaceAbstractDispatch` 报告 `unavailable`（可附原因）、`unverified: …`、`verified: …` 或 `rejected: 原因`，含义分别为访问器不可用、尚未完成分派探测、分派探测已通过，以及探测或所需服务检查未通过。尚待新增 abstract 事务的分派探测时，状态为 `unverified: …`，`interfaceAbstractMethodAddition` 为 false；这也不等于不支持。该布尔值仅在接口运行时、分派访问器及必要的链接错误服务均可用并通过探测后为 true，仍须逐事务检查全部实现类。仅分派访问器不可用或被拒绝时，新增 abstract 以 63 拒绝；其余接口修改继续按各自规则验收。ART 13 在生成可用 IfTable 前即拒绝非 public 实现；增强库通过命名错误服务观察 ART 实际选中的方法对，将匹配待验证条件的错误沿原事务转换为 63。

更具体的 abstract 重声明不能由远处的 default 补足；同批移除最后一个有效实现也拒绝。多个接口组在全部验收后才共同提交。快照后加载的相关类型触发现有有界重试，达到上限时整批拒绝 113，撤回或修正后可重新提交。失败不改写旧定义、已缓存的反射／MethodHandle／JNI method ID 或预热调用点。

接口方法体修改也会重新分配所有已加载实现类及其子类的实例，保留兼容字段值，改写引用并去优化；代价随受影响实例与堆大小增加。它不会重新执行已有实例的构造器。

## 不支持的变化（以 JVMTI 错误拒绝，旧定义保持不变）

| 变化 | 错误码 | 说明 |
| --- | --- | --- |
| 方法在 static 与实例之间变化、实例方法改为 private，或已删除的方法以另一种调用方式重新加入 | `UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED`（71） | 已校验的代码会按原调用方式调用这个名称与签名，见引擎文档[调用方式不变](STRUCTURAL_REDEFINE_ENGINE.md#调用方式不变)；可改用不同的名称或签名 |
| 方法的 varargs、strict 标志变化 | `UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED`（71） | |
| 类在接口、注解、枚举之间变化 | `UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED`（70） | |
| 移除接口，以及任何失去父类型的父类变化 | `UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED`（66） | 已校验代码依赖旧类型关系，见引擎文档[收窄不支持](STRUCTURAL_REDEFINE_ENGINE.md#收窄不支持) |

方法删除/改签名与字段删除/改类型/存储变化在退役不可用时分别以 `UNSUPPORTED_REDEFINITION_METHOD_DELETED`（67）与 `UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED`（64）拒绝，native 改 Java 以 71 拒绝；新增接口与父类变化在没有探测通过的类布局时以 66 拒绝；父类变化还需要校验重定向（`capabilities()` 的 `superclassWidening`）。

## 语义冲突（会被拒绝）

- 方法新增 final，而已加载的子类覆写了它。
- 类新增 final，而已有已加载的子类（70）。
- 类新增 abstract，而该类自身仍有实例（70）；子类的实例不受影响。
- 新增的接口无法经目标类的加载器解析、不是接口，或对目标不可访问（`JVMTI_ERROR_FAILS_VERIFICATION`，62）。
- 新父类无法解析、是接口、是 final、不可访问，或新定义与已加载子类覆写了它的 final 方法（62）；新父类扩展了该类本身（`JVMTI_ERROR_CIRCULAR_CLASS_DEFINITION`，61）；新父类不扩展原父类（66，收窄）；同一批次还重定义了该类的父类、子类或新父类链上的类（66）。
- 退役不可用时，新增或改变访问级别的方法可能顶替已加载子类的接口 default/miranda 拷贝。

## 父类变化的语义

- 只支持放宽：新父类必须扩展原父类。已有实例保留自身与原父类链的字段值，新父类的字段为零值，不执行新父类的构造器；新父类在重定义前加载但不初始化。
- 虚方法按新层级分派，新父类覆写的方法对已有实例立即生效；已在执行的旧方法帧中的 `super` 调用仍按旧层级。
- ART 13 起校验新类时按名称解析到旧类，引擎在校验期间改为返回新类。若某版本的校验器走其他解析路径，重定义以 62 拒绝，旧定义保持不变。

## 类修饰符的语义

- 可见性、final、abstract、synthetic 与嵌套类的 InnerClass 标志可以改变，新类使用新修饰符。已解析的调用点与类型引用保留，ART 不对它们重新做访问检查；新的解析按新修饰符检查。

## 新增接口的语义

- 已有实例与已加载子类的实例随即成为新接口的实例。新增的接口在重定义前经目标类的加载器加载但不初始化，其静态初始化照常在首次需要时执行；该加载器随之成为这个接口的发起加载器。

## 方法种类的语义

- 删除的 native 方法与其他方法一样退役；正在执行它的线程继续执行。native 改为 Java 后，已有的调用点、缓存的反射对象与 JNI 方法 ID 执行新的 Java 方法体。
- 方法变为 abstract 后，没有实现它的子类实例被调用时抛 `AbstractMethodError`。
- 删除的构造器：字节码 `new`、JNI `NewObject*` 抛 `NoSuchMethodError`，缓存的 `Constructor.newInstance` 抛包装它的 `InvocationTargetException`。
- 删除类初始化器不影响已初始化的类，静态字段保留原值。

## 退役成员的语义

- 删除的方法和签名替换前的方法退役：字节码、JNI、MethodHandle 再调用时抛 `NoSuchMethodError`，缓存的反射 `Method.invoke` 抛包装它的 `InvocationTargetException`；已在执行的旧方法帧继续执行旧方法体。
- 退役字段：字节码、JNI、MethodHandle 访问抛 `NoSuchFieldError`；缓存的反射 `Field` 访问实例字段抛 `IllegalArgumentException`，访问静态字段抛 `NoClassDefFoundError`；VarHandle 抛 `UnsupportedOperationException`。缓存的反射 `Field` 的名称与声明类会显示为库内占位类的字段。
- 恢复同名成员会产生新成员，旧的缓存引用保持退役。恢复的方法须保持原来的 static 或实例形式，原为虚方法的不能恢复为 private。
- 发生过退役的类及其 class loader 保留到进程结束。

## ART 自身的限制

- 普通类结构重定义沿用 ART 的检查：数组、基本类型、代理类、`java.lang.String`、mirror 类、`java.lang.Thread` 及其子类不可重定义；在本线程类初始化过程中不能重定义该类；存在指针形式 JNI ID 的类不能做结构变化。
- `sun.misc.Unsafe`、`Atomic*FieldUpdater` 等缓存的原始字段 offset 不经过 ART 的任何入口；任何改变实例布局的结构重定义（包括新增字段）都会使其失效。

## 进程内的副作用

- 方法退役启用时，库通过 JVMTI `SetJNIFunctionTable` 包装 JNI 调用与字段访问；ART 设置覆盖表后会关闭本进程的 Core Platform API 检查，该检查只约束平台模块的调用方。
- 每次经标准入口重定义前，库通过目标类的定义加载器加载（不初始化）新定义引用的全部类，防止 ART 在事务中途才加载子类、使其留在旧类之下（见 [结构重定义引擎](STRUCTURAL_REDEFINE_ENGINE.md#事务中的类加载)）。事务期间库启用 JVMTI `ClassPrepare` 事件。若仍有目标类的子类在事务中加载（例如 ClassFileLoadHook 改写后才引用它，或其他线程并发加载），该次尝试会在提交前失败并自动重试，ClassFileLoadHook 随之再次被调用；连续三次失败时返回 `JVMTI_ERROR_INTERNAL`。

## 语义说明

- 已有对象的新增字段取零值或 `null`，不重新执行构造器；新建对象执行新构造器。
- 结构变化会让 ART 遍历堆迁移实例、去优化所有线程帧并清空 JIT 代码，耗时随堆大小增加，提交后需要重新预热。
- 补丁分发、签名校验和访问控制由调用方负责；`RetransformClasses` 不属于增强范围。
