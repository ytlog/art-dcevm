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

此能力仅在 Android 13（API 33）、Android 14（API 34）、Android 15（API 35）和 Android 16（API 36）的 arm64 设备上可用；其他版本停用并在 `capabilities()` 中说明原因。运行时声明仍须通过探测，版本号本身不保证可用。

| 接口目标的拒绝情况 | JVMTI 错误码 |
| --- | --- |
| 新增 native；新增 abstract 无有效实现或 IfTable 探测不通过；direct 方法遮蔽继承的接口／Object 方法；default 遮蔽类链 static／非 public 方法或产生 default 冲突 | `METHOD_ADDED`（63） |
| 成员映射不可用时删除或改签名方法 | `METHOD_DELETED`（67） |
| 字段修饰符变化；成员映射不可用时的字段增删或改类型 | `SCHEMA_CHANGED`（64） |
| 接口方法 abstract 与 default 互换及调用方式变化之外的修饰符变化；改为 abstract 而 IfTable 探测不通过；改为 default 后会遮蔽类链 static／非 public 方法 | `METHOD_MODIFIERS_CHANGED`（71） |
| 成员映射或分派封闭不可用时移除父接口；成员映射不可用时新增父接口；新父接口的方法被 static／private 方法遮蔽，或其 default 遮蔽类链 static／非 public 方法；新父接口的 abstract 方法缺少 IfTable 探测 | `HIERARCHY_CHANGED`（66） |
| 新增的接口由同批重建，而新类与该重建互相依赖，或同一重建组内新类排在它之前 | `HIERARCHY_CHANGED`（66） |
| 类在接口、注解、枚举之间变化（接口的访问级别与 synthetic 等修饰符可像普通类一样变化） | `CLASS_MODIFIERS_CHANGED`（70） |
| 接口重建组之外的实现类父类在同批改变父类 | `HIERARCHY_CHANGED`（66） |
| boot 接口、当前使用指针型 JNI ID，或目标／任一子类型带 pointer-id 标记 | `UNMODIFIABLE_CLASS`（79） |
| 有 Proxy 实现类的接口改变父接口 | `HIERARCHY_CHANGED`（66） |
| 运行时版本、必需命名服务或声明探测不可用 | `NOT_AVAILABLE`（98） |

删除的 default/abstract 方法与普通类的退役方法相同：经接口或实现类的字节码调用、缓存的反射、JNI 与 MethodHandle 抛 `NoSuchMethodError`，实现类自身声明的同名方法保留；改签名即旧签名退役、新签名新增。default 改为 abstract 与新增 abstract 一样，要求每个已加载具体实现类都有有效实现，否则以 63 拒绝；abstract 改为 default 后，未声明实现的实现类获得 copied default。接口在重定义前已初始化，新增的静态字段取默认值；与普通类相同，新增或修改的 `<clinit>` 不会在已初始化的接口上执行；常量表达式通常已被调用方内联。新增父接口时，新父接口及其目标原先未继承的父接口中的 abstract 方法，与新增 abstract 一样要求每个已加载具体实现类都有有效实现，否则以 63 拒绝，原因写明新父接口方法与缺实现的类；其 default 由实现类获得 copied default。

`lastRejection` 给出被拒类名和原因；失败不改变旧行为。 新 default 对已有实例立即生效，没有提交 DEX 的实现类也获得 copied default；类链 public 实现与更具体的子接口仍优先。static/private 退役沿用普通类的字节码、JNI、反射与 MethodHandle 语义，default 冲突原因写明实现类和方法。插入 lambda 可能改变合成类序号，热重载端须像处理普通类一样隔离旧闭包或保留旧桥，见[接口成员演进](STRUCTURAL_REDEFINE_ENGINE.md#接口成员演进)。

接口事务要求 ART 可以改写已有成员的 JNI ID。纯 pointer 模式可能没有 ClassExt 的 pointer-id 标记，因此在 Native 快照阶段还检查公开 JVMTI 返回的方法或字段 ID 表示；指针 ID 在进入 ART 事务前以 79 拒绝。不能仅切换模式来修复此前缓存的指针 ID。普通类仍按其原有规则验收。

新增 public abstract 方法要求重定义后，接口组内**每个已加载的具体实现类**都能通过 ART 的实际 IfTable 分派到同名、完整 descriptor 相同的 public 具体实例方法，包括没有实例、没有提交 DEX 的类。同批补齐实现不要求提交顺序；已有实现、父类继承的实现和 ART 实际选中的更具体 default 均可接受。缺少实现、abstract/miranda、static/private/非 public 方法或 default 冲突均整批拒绝 63，不推迟到调用时抛 `AbstractMethodError`。泛型与协变返回依赖真实 bridge，不把相同名称或不同返回类型当作实现；同名类型按类加载器身份分别判断。`lastRejection` 写出接口方法及缺实现的类，撤回或补齐后可重试。

新增 abstract 需要逐事务完成实际分派验收。分派访问器或所需链接错误服务不可用时，新增 abstract 以 63 拒绝；其他接口修改仍按各自规则验收。ART 13 在生成可用 IfTable 前拒绝非 public 实现，增强库观察 ART 实际选中的方法对，只将匹配待验证条件的错误沿原事务转换为 63。ART 14 的配置也要求该命名链接错误服务；ART 15、16 验收已链接的分派数组。

更具体的 abstract 重声明不能由远处的 default 补足；同批移除最后一个有效实现也拒绝。多个接口组在全部验收后才共同提交。快照后加载的相关类型触发现有有界重试，达到上限时整批拒绝 113，撤回或修正后可重新提交。失败不改写旧定义、已缓存的反射／MethodHandle／JNI method ID 或预热调用点。

子类型相交的接口组，以及同批做结构修改、又是组内类型父类的组外类，合并为一次重建，每个类型只替换和迁移一次，与提交顺序无关。ART 的实例收集只覆盖拥有者条目所属类的实例；没有一个根覆盖全部具体成员时，引擎让 ART 以其余各根依次再收集一轮，每个实例只保留在它最先可赋值的根那一轮，因此无关接口共享实现类、接口与组外父类各有独立实例时同样只迁移一次。组外父类只改方法体时不并入重建，仍原地更新。

接口方法体修改也会重新分配所有已加载实现类及其子类的实例，保留兼容字段值，改写引用并去优化；代价随受影响实例与堆大小增加。它不会重新执行已有实例的构造器。

`java.lang.reflect.Proxy` 生成的实现类（例如 Retrofit 服务或注解实例）保留原类与 `InvocationHandler`：保留的方法照常交给处理器，处理器收到的是新接口的 `Method`；Proxy 没有的新增 abstract 方法调用时抛 `AbstractMethodError`，新增 default 执行其方法体；之后用同一接口创建的代理复用这个类，行为相同，与 JBR 一致。新增 abstract 方法不因 Proxy 缺实现而拒绝（Proxy 没有可以同批补齐的源码），普通实现类仍按上文验收。

## 不支持的变化（以 JVMTI 错误拒绝，旧定义保持不变）

| 变化 | 错误码 | 说明 |
| --- | --- | --- |
| 方法在 static 与实例之间变化、实例方法改为 private，或已删除的方法以另一种调用方式重新加入，而批次之外仍有已加载类的代码按旧调用方式引用它，或批次类有活动帧 | `UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED`（71） | 已校验的代码会按原调用方式调用这个名称与签名，见引擎文档[调用方式变化](STRUCTURAL_REDEFINE_ENGINE.md#调用方式变化)；把这些调用方同批提交即可 |
| 类在接口、注解、枚举之间变化 | `UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED`（70） | |
| 失去原有父类的父类变化 | `UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED`（66） | 已校验代码依赖旧类型关系，见引擎文档[收窄不支持](STRUCTURAL_REDEFINE_ENGINE.md#收窄不支持) |
| 普通类移除接口，而该类属于本批某个接口组（实现了同批重定义的接口，或是组成员的父类），或成员映射、分派封闭不可用 | `UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED`（66） | 见[移除接口的语义](#移除接口的语义) |

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
- 仍把这些对象当作被移除接口调用的已校验代码，以及字段、数组中以该接口类型保存的旧引用，调用其方法时抛 `IncompatibleClassChangeError`，与 JVM 相同；解释执行、JIT 编译后的代码与 boot 代码（例如 `Thread.run` 调用 `Runnable.run`）都如此。该类自己以被移除接口声明的静态字段若值已不实现该接口则置为 `null`（与 JBR 相同）；其他旧引用不会被置空。
- 类从被移除接口继承的 default 方法随之消失：以类本身调用它的旧字节码抛 `NoSuchMethodError`。
- 这些类的接口调用经分派槽位查找执行，会稍慢。之后加载的具体子类继承这一行为；失去接口的抽象类之后才加载的具体子类除外，见引擎文档[移除接口](STRUCTURAL_REDEFINE_ENGINE.md#移除接口)。
- 接口目标也可以移除父接口：其子接口、实现类及其子类随之失去该父接口，语义同上；有 Proxy 实现类时以 66 拒绝。
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

- 普通类结构重定义沿用 ART 的检查：数组、基本类型、代理类、`java.lang.String`、mirror 类，以及 `java.lang.Thread` 本身和它的父类型不可重定义；应用自己的 `Thread` 子类可以重定义，已启动线程的对象身份、名称、ID、中断与 join 保持不变，正在执行的 `run()` 帧继续执行旧方法体；在本线程类初始化过程中不能重定义该类；存在指针形式 JNI ID 的类不能做结构变化。
- `sun.misc.Unsafe`、`Atomic*FieldUpdater` 等缓存的原始字段 offset 不经过 ART 的任何入口；任何改变实例布局的结构重定义（包括新增字段）都会使其失效。

## 进程内的副作用

- 方法退役启用时，库通过 JVMTI `SetJNIFunctionTable` 包装 JNI 调用与字段访问；ART 设置覆盖表后会关闭本进程的 Core Platform API 检查，该检查只约束平台模块的调用方。
- 每次经标准入口重定义前，库通过目标类的定义加载器加载（不初始化）新定义引用的全部类，防止 ART 在事务中途才加载子类、使其留在旧类之下（见 [结构重定义引擎](STRUCTURAL_REDEFINE_ENGINE.md#事务中的类加载)）。事务期间库启用 JVMTI `ClassPrepare` 事件。若仍有目标类的子类在事务中加载（例如 ClassFileLoadHook 改写后才引用它，或其他线程并发加载），该次尝试会在提交前失败并自动重试，ClassFileLoadHook 随之再次被调用；连续三次失败时返回 `JVMTI_ERROR_INTERNAL`。

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
- 补丁分发、签名校验和访问控制由调用方负责；`RetransformClasses` 不属于增强范围。

## 与 JBR DCEVM 的对照

下表对照 JetBrains Runtime 的增强重定义（`-XX:+AllowEnhancedClassRedefinition`），只列两者语义不同或需要说明的地方；方法、字段、修饰符、父类放宽、新增与移除接口、接口成员、Proxy 实现类与 `Thread` 子类的结构变化两者都接受。

| 变化 | JBR DCEVM | art-dcevm |
| --- | --- | --- |
| 接口新增 abstract 方法，而已加载的实现类没有实现 | 接受，调用时抛 `AbstractMethodError` | 拒绝 63，提交前指出缺实现的类；Proxy 实现类除外，与 JBR 相同 |
| 方法在 static 与实例之间变化，或改为 private，而批次外已加载代码仍按旧方式调用 | 接受，调用时重新解析并抛 `IncompatibleClassChangeError` | 拒绝 71：ART 重新解析时不检查调用方式，无法抛出该错误；同批提交调用方即可接受 |
| 父类收窄（新父类链不含原父类） | 拒绝 | 拒绝 66 |
| 失去接口的抽象类之后才加载的具体子类 | 按新层级正确分派 | 可能分派到同一 IMT 槽位的其他方法，见[引擎风险](STRUCTURAL_REDEFINE_ENGINE.md#风险) |
| 类在普通类与接口等种类之间变化 | 接受，旧调用点抛 `IncompatibleClassChangeError` | 拒绝 70 |
| boot 类、`Thread` 及其父类型、数组、代理类自身 | 部分可重定义 | 按 ART 的规则拒绝 |
| 新类（新 lambda、匿名类） | 由 HotswapAgent 等工具定义 | 由热重载工具追加到加载器，见[提供新类](#提供新类) |
