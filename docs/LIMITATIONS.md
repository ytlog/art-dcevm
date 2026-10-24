# 能力限制

[English](LIMITATIONS.en.md) | 中文

本文汇总使用时需要知道的前提、会被拒绝的变化以及各类变化的语义。实现机制见 [STRUCTURAL_REDEFINE_ENGINE.md](STRUCTURAL_REDEFINE_ENGINE.md)。

## 提交与错误处理

初始化成功后，直接通过调用方 Agent 的标准 `jvmtiEnv::RedefineClasses` 提交完整批次。返回 `0`（`JVMTI_ERROR_NONE`）表示成功；任何非零返回值表示失败，读取该错误码及 `DcevmRuntime.capabilities()` 中的 `lastRejection`，旧定义保持不变。`lastRejection` 描述最近一次标准入口事务的失败原因，成功时清空。

`capabilities()` 只用于诊断，不作为提交前的准入判断；不要用其中的布尔值或状态字符串拦截提交。准入与支持的变化由实际事务检查决定。诊断字段参考见[测试指南](TESTING.md#诊断报告参考)。

标准 `RedefineClasses` 与增强 `RetransformClasses` 在持有进程级可重入事务锁时调用 ART，期间同步执行各 Agent 的 `ClassFileLoadHook`。回调允许在同一线程发起嵌套事务；不得阻塞等待另一个正在调用或需要进入 `RedefineClasses`／`RetransformClasses` 的线程，否则该线程等待事务锁、回调等待该线程，会造成死锁。

## 前提限制

- 应用必须从进程启动时就是 Java 可调试的；release 进程中 ART 不开放 `can_redefine_classes`，本库无法工作。
- 仅支持 arm64-v8a 的 Android 11–17。
- 运行库缺少基础入口的任一命名符号，或公开 JVMTI 表与找到的命名包装器不一致时初始化失败。方法与字段退役另需各自的名称；缺少时只关闭相应能力，并在 `capabilities()` 的诊断中说明。
- 退役依赖运行时探测确认 ART 的类布局声明；探测不通过的事务不允许退役，`capabilities()` 的 `memberMapping` 给出原因。
- 增强范围是标准 `RedefineClasses`。标准入口的批次读取和事务执行已串行化；直接调用 ART 结构重定义扩展的并发事务不参与该保护。

成员退役还要求 Native 阶段的线程与命名读屏障服务检查通过；线程或类布局检查失败时按原有错误规则拒绝。设备覆盖及尚待验证的版本见[兼容性结论](COMPATIBILITY.md)。

## 接口重定义

整批类粒度分配接受固定 JBR 对照中的 s1/s3/s4/s5/s6，s2 保持 66。旧实例保留自身和旧祖先链的兼容字段，新插入父类字段为零。JBR 21.0.11+10-b1163.116 在 s1/s4 的该字段读到 3；项目有意保留零值语义。见 [对照表](COMPATIBILITY.md#同批次类组合与-jbr-对照)。

接口目标支持 default、static、private 方法体修改，新增 public abstract、default、static（任意访问级别，含 synthetic）、private 实例方法，删除或改签名任一方法（含 default/abstract），已有 public 实例方法在 abstract 与 default 之间切换，方法在 default/abstract、static、private 之间改变调用方式（规则同普通类，见下表 71），新增、删除或改类型静态字段，以及新增或移除父接口。顶层与嵌套接口可与实现类、实现类子类或无关类同批，也可提交多个接口和父子接口；任一目标失败时整批保持旧定义。lambda 等新合成类须由热重载端先追加到加载器，发生变化的已有合成类须在同批重定义。

此能力在 Android 11–17（API 30–37）的 arm64 设备上可用。API 33–36 已经真机验证；API 30–32 与 37 依固定 AOSP 声明及这些版本全部已存设备运行库中的命名服务启用，尚未在设备上运行接口套件，探测通过后 `interfaceRuntime` 会注明这一点。运行时声明仍须通过探测，版本号本身不保证可用。

| 接口目标的拒绝情况 | JVMTI 错误码 |
| --- | --- |
| 新增 native；遮蔽 Object 方法，或符号守卫不可用时遮蔽继承的接口方法；选择服务不可用时 default 与非 public 类方法同签名或旧 ART 无法链接非 public 实现 | `METHOD_ADDED`（63） |
| 成员映射不可用时删除或改签名方法 | `METHOD_DELETED`（67） |
| synthetic 之外的字段修饰符变化；成员映射不可用时的字段变化 | `SCHEMA_CHANGED`（64） |
| 接口方法描述标志、abstract 与 default 互换及调用方式变化之外的修饰符变化；选择服务不可用时改为 default 后出现上述同签名 | `METHOD_MODIFIERS_CHANGED`（71） |
| 成员映射或分派封闭不可用时移除父接口；成员映射不可用时新增父接口；符号服务不可用时新父接口的方法被 static／private 方法遮蔽，或选择服务不可用且其 default 出现上述同签名 | `HIERARCHY_CHANGED`（66） |
| class/interface 跨界时存在已加载子类型；需要实例前置检查却能力不可用或存在直接实例 | 分别为 62、70；允许的种类转换见[转换矩阵](CLASS_KIND_EVOLUTION.md) |
| boot 接口、当前使用指针型 JNI ID，或目标／任一子类型带 pointer-id 标记 | `UNMODIFIABLE_CLASS`（79） |
| 运行时版本、必需命名服务或声明探测不可用 | `NOT_AVAILABLE`（98） |

删除的 default/abstract 方法与普通类的退役方法相同：经接口或实现类的字节码调用、缓存的反射、JNI 与 MethodHandle 抛 `NoSuchMethodError`，实现类自身声明的同名方法保留；改签名即旧签名退役、新签名新增。default 改为 abstract 与新增 abstract 相同，未实现它的类调用时抛 `AbstractMethodError`；abstract 改为 default 后，未声明实现的实现类获得 copied default。接口在重定义前已初始化，新增的静态字段取默认值；与普通类相同，新增或修改的 `<clinit>` 不会在已初始化的接口上执行；常量表达式通常已被调用方内联。新增父接口时，新父接口及其目标原先未继承的父接口中的 abstract 方法与新增 abstract 相同；其 default 由实现类获得 copied default。已有 private/static 声明可以新增同签名 default/abstract 父接口：自身调用保留，父接口独立分派，static 遮蔽沿用 fresh MethodHandle 查找保护。存在批次类型活动帧时，前后保持 public static 的已有声明可在已安装并探测通过的 copied default 旧帧归属服务及成员映射保护下新增或移除同签名父接口方法：旧帧继续调用自身 static，父接口独立分派。已有 private 实例声明保持 private 时也可保留活动帧：要求旧帧归属保护与 Class/collector 探测。现代 ART 通过命名 `NterpGetMethod`／`ResolveMethodId` 按描述符及定义加载器选择自身目标；旧版通过命名 `ResolvedMethodAccessTest<true>`／`GetObsoleteClass` 确认调用方是当前声明的真实历史 Class。API 33／36 的新增、移除、回滚、多次提交中的同一旧帧及 GC 已验证；缺少相应服务或改变声明调用方式时保留 71。固定 JBR 还存在新增 abstract 父接口后的缓存反射／MethodHandle 槽别名，以及成功事务中 private 自身旧帧抛 ICCE 的行为；这些结果独立记录，不作为 ART 应复制的安全行为。

移除父接口后，若方法已不属于最终层级，旧的子接口符号字节码调用抛 `NoSuchMethodError`。新增 private/static 声明遮蔽父接口方法时，旧子接口调用分别抛 `IllegalAccessError`／`IncompatibleClassChangeError`，包括 null 接收者；仍实现父接口的接收者保留父接口调用及缓存的父声明反射、JNI、MethodHandle 引用。对象丢失父接口时，缓存反射抛 `IllegalArgumentException`，子接口句柄抛 ICCE，缓存类虚句柄抛 AME，父接口类型的句柄在接收者转换时抛 `ClassCastException`。static 遮蔽后，新建 `findVirtual(子接口, ...)` 对匹配的完整签名抛 `IllegalAccessException`，返回类型不匹配仍抛 `NoSuchMethodException`。父接口查找、自身 static 方法的 `findStatic`／反射及旧缓存句柄保持原行为。首个接口批次在 Native 阶段验证并启用公开 JVMTI 查找保护；准备或安装失败以 71 拒绝该批次。运行中已验证的查找参数不可读时显式抛 `InternalError`，不错误返回父方法。首个接口批次完成 Native 准备后，守卫常驻：在 `getInstanceMethod` 直接遍历的声明上，普通类与接口的参数／返回类型仅同名、实际 Class 来自另一加载器时，对匹配的完整描述符抛 `IllegalAccessException`；错误描述符仍交给原路径。实际参数类型由 ART 的反射转换提供，不另建层级解析器。bootstrap hidden-API 与 obsolete mirror 继续由原入口检查。对 public 应用接口的 `findStatic`，复用命名 ART 接口解析器：完整描述符匹配到继承实例方法时抛 `IllegalAccessException`，无匹配抛 `NoSuchMethodException`；自身 static 保留加载器身份与访问检查，null 及错误描述符保持独立预期。static 路径还要求独立服务与活参数证明。多层父接口及实现类的递归实例查找同样检查加载器身份：仅匹配固定 `Class.getPublicMethodRecursive/findInterfaceMethod` 链至 `getInstanceMethod/Lookup.findVirtual`，从实际深度读取活 `MethodType`。递归活参数证明是准入前置条件；普通反射与不匹配调用链不受接管。

同一查找保护也覆盖 public 应用普通类的已声明静态方法：`findStatic` 的完整描述符匹配、但参数或返回 Class 来自另一加载器时抛 `IllegalAccessException`。匹配类型的调用、对象身份、null 参数、错误描述符和普通反射保留原路径。static 准备另要求观察普通类活 `MethodType` 并实际调用内部静态方法成功；bootstrap 与未通过独立证明的非 public 类仍交回原入口。

通过独立继承静态调用证明后，public 应用类的 `findStatic` 还支持通过子类查找父类静态方法。命名 `Class::FindClassMethod` 按完整描述符选择声明，公开 JVMTI method ID 转成真实反射对象，再交回 libcore 检查访问权限与创建句柄；不改变普通 `getDeclaredMethod` 的声明范围。类内遮蔽、重载、类型加载器身份、private/protected 访问拒绝、非 public 父类的 public 方法，以及父类改体后的新旧句柄在 API 33／36 已验证。缺少命名解析器或独立调用证明时保留原查找路径；bootstrap 声明继续由原入口过滤。其他已存运行库仅核对命名符号，不能视作设备验证；API 30–32 的旧帧续接 71 不变。

通过独立非 public 类证明后，普通应用类的 static 查找不再仅限 public 类：同一运行时包的合法 Lookup 可以查找其声明或继承的静态方法，完整描述符匹配但参数／返回 Class 属于另一加载器时抛 IllegalAccessException。查找保护不授予访问权，libcore 继续检查 Lookup；publicLookup、无访问权限的 Lookup，以及包名相同但加载器不同的 Lookup 仍被拒绝。证明分别要求观察声明与继承两条路径、实际调用成功和受限查找被拒绝；任一缺失只关闭此扩展，原 public 类静态查找保留。bootstrap 与非 public 接口仍委托原入口。

通过独立桥接调用证明后，`findVirtual` 对普通应用类和接口的协变返回值桥接方法按完整描述符选择真实声明。例如同时存在 `String value()` 与编译器生成的 `Object value()` 时，合法的 Object 句柄不再被参数式反射查找错误拒绝。仅接管精确匹配的 ACC_BRIDGE 实例声明，原 libcore 仍检查访问权限并创建句柄；普通反射、非桥接声明和 bootstrap 路径保持原处理。证明必须观察普通类、直接接口及递归接口三条路径并实际调用成功，否则只禁用此扩展。API 33／36 覆盖继承类、接口多层继承、实现类、子类覆写、null、方法体异常、完整句柄类型、精确调用，以及父类改体后新旧句柄、失败事务、恢复与 GC。

`getMethods()` 的继承应用接口声明现在按完整签名保留更具体的接口声明，包含协变桥接和普通实例方法。原问题是父接口先收集、去重时留下祖先 abstract 声明，并非桥接方法从 native 数组中消失。独立运行时证明后，在固定的内部收集调用链中使用 ART 已创建的 Method 替换被覆盖条目；原过滤与最终去重保留。类实现优先、不同返回类型／参数 Class 身份、普通 `getDeclaredMethods()` 和 bootstrap 枚举保持原行为。API 33／36 验证桥接／synthetic／default 属性、声明类、两个方向的菱形父接口顺序、真实反射调用、缓存 Method、改体／回滚／恢复与 GC。其他运行库仅有入口名称覆盖，不代表设备验证。

此扩展不改变无继承关系接口之间的同签名去重策略，也不扩大 ART 原生可见性过滤；bootstrap 根的 static 例外见下文独立证明。准备期间缺入口、活列表／当前 Class 读取失败、部分证明或实际调用失败均不启用；启用后遇到不可读服务抛 InternalError 或保留原异常，不返回部分修正结果。首个可用接口事务的 Native 阶段准备，成功启用后驻留，包括随后事务失败的情况。新增枚举路径的性能尚未单独验收；原性能越线和停顿问题保持记录。


`lastRejection` 给出被拒类名和原因；失败不改变旧行为。 新 default 对已有实例立即生效，没有提交 DEX 的实现类也获得 copied default；类链 public 实现与更具体的子接口仍优先。static/private 退役沿用普通类的字节码、JNI、反射与 MethodHandle 语义。与 JBR 相同，新出现的 default 冲突照常接受，经冲突方法的调用抛 `IncompatibleClassChangeError`，之后消除冲突的事务恢复唯一的 default。插入 lambda 可能改变合成类序号，热重载端须像处理普通类一样隔离旧闭包或保留旧桥，见[接口成员演进](STRUCTURAL_REDEFINE_ENGINE.md#接口成员演进)。

接口事务要求 ART 可以改写已有成员的 JNI ID。纯 pointer 模式可能没有 ClassExt 的 pointer-id 标记，因此在 Native 快照阶段还检查公开 JVMTI 返回的方法或字段 ID 表示；指针 ID 在进入 ART 事务前以 79 拒绝。切换模式不会清除历史 pointer-id 准入标记。普通类仍按 ART 原有规则验收；声明在另一接口上的生成 copy，其保留指针缓存另见[移除接口](#移除接口的语义)。

与 JBR 相同，新增 public abstract 方法不要求已加载的实现类提供实现：已有实现、继承实现、同批补齐的实现与 ART 选中的更具体 default 照常分派；缺少实现、缺少真实 bridge 或被更具体 abstract 遮住的 default，在调用时抛 `AbstractMethodError`。`interfaceMethodSelection=true` 时，package-private/protected 类虚方法（含另一运行时包的父类方法）在接口选择中优先于 default，事务成功，接口调用抛 `IllegalAccessError`；原有类方法调用保留原行为。private 方法本身不实现新增 abstract，调用抛 `AbstractMethodError`。

新增 default、abstract/default 互换与新增父接口带来的方法共用同一选择路径。private/static 声明不作为接口实现；更具体的 public 类覆写照常优先。private/default 的结果取决于接口进入类链的位置：该类首次实现接口时可以选择 default；类链有 private、且从父类继承接口 default 时选择 `AbstractMethodError`，包括自身重声明 private 与后加载子类。详见[选择矩阵](STRUCTURAL_REDEFINE_ENGINE.md)。命名链接、分配与错误服务缺失时 `interfaceMethodSelection=false`，保留原有保守拒绝；平台验证记录见[兼容性说明](COMPATIBILITY.md)。

多个接口组共同提交。快照后加载的相关类型触发现有有界重试，达到上限时整批拒绝 113，撤回或修正后可重新提交。失败不改写旧定义、已缓存的反射／MethodHandle／JNI method ID 或预热调用点。

子类型相交的接口组，以及同批做结构修改、又是组内类型父类的组外类，合并为一次重建，每个类型只替换和迁移一次，与提交顺序无关。ART 的实例收集只覆盖拥有者条目所属类的实例；没有一个根覆盖全部具体成员时，引擎让 ART 以其余各根依次再收集一轮，每个实例只保留在它最先可赋值的根那一轮，因此无关接口共享实现类、接口与组外父类各有独立实例时同样只迁移一次。组外父类只改方法体时不并入重建，仍原地更新。

接口方法体修改也会重新分配所有已加载实现类及其子类的实例，保留兼容字段值，改写引用并去优化；代价随受影响实例与堆大小增加。它不会重新执行已有实例的构造器。

`java.lang.reflect.Proxy` 生成的实现类（例如 Retrofit 服务或注解实例）保留原类与 `InvocationHandler`：保留的方法照常交给处理器，处理器收到的是新接口的 `Method`；Proxy 没有的新增 abstract 方法调用时抛 `AbstractMethodError`，新增 default 执行其方法体；之后用同一接口创建的代理复用这个类，行为相同，与 JBR 一致。接口新增父接口时，已有和之后创建的代理都成为该父接口的实例，与 JBR 一致：Proxy 原本没有的父接口方法执行父接口的 default 方法体或抛 `AbstractMethodError`，Proxy 原本就有的方法仍交给处理器。新增 abstract 方法不因 Proxy 缺实现而拒绝（Proxy 没有可以同批补齐的源码），普通实现类同样在调用时按上文规则抛错。父接口按整批的最终关系统一更新：支持移除与恢复、多个接口共同新增父接口，以及新父接口同批重定义；还有其他继承路径的父接口保留，最后一条路径移除后代理才失去该类型。

接口方法的 synthetic、bridge、varargs、strict，以及字段的 synthetic，可经公共结构修饰符路径双向变化。描述标志变化保留成员身份、缓存调用目标和兼容字段值；新反射对象报告新标志，已有反射对象保留创建时的标志，与固定 JBR 一致。接口访问级别及其他字段修饰符限制继续适用。

## 不支持的变化（以 JVMTI 错误拒绝，旧定义保持不变）

| 变化 | 错误码 | 说明 |
| --- | --- | --- |
| 虚方法改为 private，或已删除的虚方法以 private 方式重新加入，而检测到该目标类的活动帧且缺少自身调用选择能力 | `UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED`（71） | JBR 可让自身旧帧直接调用新 private 方法。按目标种类安装命名 `FindMethodToCall<kVirtual/kInterface>` 入口并通过 Class/Thread/HandleScope 探测后支持此行为，分别以 `privateSelfSelectionClass`／`privateSelfSelectionInterface` 报告，旧键为交集。API 36 已经真机验证；部分 API 34/37 仅保留 interface 模板的模块按固定源码支持接口目标，尚未上机。API 30–32 和缺少对应命名服务或源码依据的目标仍拒绝 71；API 33 另由下述真实续接证明准入。无访问权的批次外旧调用方可保留，调用时抛 IAE，见[调用方式变化](STRUCTURAL_REDEFINE_ENGINE.md#调用方式变化) |
| 种类变化不满足实例、子类型、分配或分派前提 | 62／66／70 | 见[类种类演进](CLASS_KIND_EVOLUTION.md) |
| 失去原有父类的父类变化 | `UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED`（66） | 已校验代码依赖旧类型关系，见引擎文档[收窄不支持](STRUCTURAL_REDEFINE_ENGINE.md#收窄不支持) |
| 移除接口时成员映射或分派保护不可用 | `UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED`（66） | 见[移除接口的语义](#移除接口的语义) |

方法删除/改签名与字段删除/改类型/存储变化在退役不可用时分别以 `UNSUPPORTED_REDEFINITION_METHOD_DELETED`（67）与 `UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED`（64）拒绝，native 改 Java 以 71 拒绝；新增接口与父类变化在没有探测通过的类布局时以 66 拒绝；父类变化还需要校验重定向。

## 语义冲突（会被拒绝）

- 方法新增 final，而已加载的子类覆写了它。
- 类新增 final，而已有已加载的子类（70）。
- 类新增 abstract，而该类自身仍有实例（70）；子类的实例不受影响。
- 新增的接口无法经目标类的加载器解析、不是接口，或对目标不可访问（`JVMTI_ERROR_FAILS_VERIFICATION`，62）。
- 新父类无法解析、是接口、final、不可访问或其 final 方法被覆写时为 62；新父类反向扩展目标时为 61；不扩展原父类（收窄）时为 66。同一旧父类子树内父子两个目标同时改父类仍为 66。需要统一分配的结构依赖在该能力不可用时为 66。结构祖先、新父类链、接口与组外实现类父类的组合改动按整批最终层级分配，见 [JBR 对照](COMPATIBILITY.md#同批次类组合与-jbr-对照)。
- 退役不可用时，新增或改变访问级别的方法可能顶替已加载子类的接口 default/miranda 拷贝。

## 父类变化的语义

- 只支持放宽：新父类必须扩展原父类。已有实例保留自身与原父类链的字段值，新父类的字段为零值，不执行新父类的构造器；新父类在重定义前加载但不初始化。
- 这与固定 JBR 的实例复制有已证实冲突：其更新计划先按名称／签名查找旧目标字段，未找到时仍可采用新父类声明的历史字段偏移，从旧对象该偏移复制字节。新父类字段因而可能得到旧目标其他字段的值，并非执行了新父类构造器。项目保留上面的零值契约，此项不宣称已对齐。
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
- 首次接口收窄后，分派保护持续到进程结束，覆盖后续结构重建与新链接的类，包括抽象父类的晚加载具体子类。接口调用需要查找；局部调用与类加载成本见[引擎风险](STRUCTURAL_REDEFINE_ENGINE.md#风险)，完整应用与字段快照成本仍待量化。
- 接口目标也可以移除父接口：其子接口、实现类及其子类随之失去该父接口，语义同上；Proxy 实现类按整批最终接口图移除该父接口，与 JBR 一致。
## 方法种类的语义

- 删除的 native 方法与其他方法一样退役；正在执行它的线程继续执行。native 改为 Java 后，已有的调用点、缓存的反射对象与 JNI 方法 ID 执行新的 Java 方法体。
- 方法变为 abstract 后，没有实现它的子类实例被调用时抛 `AbstractMethodError`。
- 删除的构造器：字节码 `new`、JNI `NewObject*` 抛 `NoSuchMethodError`，缓存的 `Constructor.newInstance` 抛包装它的 `InvocationTargetException`。
- 删除类初始化器不影响已初始化的类，静态字段保留原值。

## 退役成员的语义

- 删除的方法和签名替换前的方法退役：字节码、JNI、MethodHandle 再调用时抛 `NoSuchMethodError`，缓存的反射 `Method.invoke` 抛包装它的 `InvocationTargetException`；已在执行的旧方法帧继续执行旧方法体。
- 退役字段：字节码、JNI、MethodHandle 访问抛 `NoSuchFieldError`；缓存的反射 `Field` 访问实例字段抛 `IllegalArgumentException`，访问静态字段抛 `NoClassDefFoundError`；VarHandle 抛 `UnsupportedOperationException`。缓存的反射 `Field` 的名称与声明类会显示为库内占位类的字段。
- 恢复同名成员会产生新成员，旧的缓存引用保持退役。恢复时 static／实例方式互换的旧调用按调用方式保护抛 ICCE；原虚方法恢复为 private 后，无访问权的旧外部调用抛 IAE；自身旧活动帧须具备对应种类的选择服务及布局探测。
- 发生过退役的类及其 class loader 保留到进程结束。

缓存反射构造器具有访问器历史：公有查询和声明查询分别共享根；冷访问器首次在不可实例化状态调用后，恢复普通类仍抛 `InstantiationException`；已取得的可调用访问器先检查参数，再包装分配或退役错误。成功事务的新查询使用新根，失败事务保留历史，详见[类种类演进](CLASS_KIND_EVOLUTION.md)。弱引用身份检查，以及切换缓存构造器时的标签查询，会增加调用成本，需在目标应用测量。初始化前／序列化发现、并发构造活动帧、全部非 public 访问组合及软引用反射缓存回收后的共享关系尚未完成证明；旧构造器在恢复后才首次调用也保留单独记录的参照差异。

## ART 自身的限制

- 普通类结构重定义沿用 ART 的检查：数组、基本类型、代理类、`java.lang.String`、mirror 类，以及 `java.lang.Thread` 本身和它的父类型不可重定义；应用自己的 `Thread` 子类可以重定义，已启动线程的对象身份、名称、ID、中断与 join 保持不变，正在执行的 `run()` 帧继续执行旧方法体；在本线程类初始化过程中不能重定义该类；目标或相关子类型的历史 pointer-id 标记会阻止结构变化（79）；持有从另一声明接口获得的 copied default 指针 ID，并不等于目标类自身带有这一标记。成功处理这类缓存不能外推为任意指针模式结构更新。
- `sun.misc.Unsafe`、`Atomic*FieldUpdater` 等缓存的原始字段 offset 不经过 ART 的任何入口；改变实例布局的结构重定义可能使其失效。固定 JBR 的独立探针也测得原始 offset、旧 FieldUpdater 及未建立访问器的缓存 Field 仍读取旧偏移；这不是所有布局均已对齐的证明。原始 offset 的通用对齐路径仍未建立。

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
- 补丁分发、签名校验和访问控制由调用方负责。arm64 Android 11–17（含 12L）的 `RetransformClasses` 将 ClassFileLoadHook 最终 DEX 送入增强结构路径，使用相同的新增与退役规则；11–12L 尚缺 arm64 设备验证。需两个路由命名服务、旧版懒展开服务、匹配的公开函数表和固定声明校验通过；不通过则回退 ART 原生规则。路由及其重试不重复调用 ClassFileLoadHook，`retransformRouted` 报告安装能力。

## 与 JBR DCEVM 的对照

目标语义以固定 JBR DCEVM 源码和对应运行时对照为准（`-XX:+AllowEnhancedClassRedefinition`）。下表记录当前差异及关键边界，不是完整功能已对齐的声明。当前以错误码拒绝某项变化，不代表已经满足对齐目标；尚有差异的路径仍需修复。

| 变化 | JBR DCEVM | art-dcevm |
| --- | --- | --- |
| 持有动态代理的实例字段改类型 | 新类型是直接声明接口或 `Proxy` 父类时保留；仅间接实现接口时清空 | 目标与继承字段按同一规则处理，见[字段改型对照](TESTING.md#jbr-对照) |
| 接口方法与非 public 类方法同签名 | 接受；private 不实现 abstract，抛 `AbstractMethodError`；package-private/protected 抛 `IllegalAccessError` | 选择服务可用时相同；类方法调用保留，支持 default/abstract 切换与新增父接口；API 33/36 已验证 |
| 普通类方法在 static 与实例之间切换，而批次外已加载代码仍按旧方式调用 | 接受，调用时重新解析并抛 `IncompatibleClassChangeError` | 命名调用方式保护可用时接受，旧调用抛 ICCE；旧调用方禁止 JIT 编译及内联，ART 仍执行原有去优化。Android 13、16 覆盖普通／CheckJNI、热调用、活动帧、GC 与隐式重建；入口不可用时保留 71 |
| 普通类虚方法改为 private，批次外仍有无访问权的旧调用方 | 接受；旧 virtual/super 调用抛 `IllegalAccessError`，自身旧帧可调用新 private 方法 | 接受并保护外部旧调用，抛 IAE；旧调用方禁止 JIT 编译及内联。有对应种类的命名选择服务时（`privateSelfSelectionClass`／`privateSelfSelectionInterface`），自身旧帧直接选择 private 方法并忽略覆写，含接口 default 副本；API 33 可由独立续接证明准入；API 30–32 或运行时探测不匹配时拒绝 71 |
| 父类收窄（新父类链不含原父类） | 拒绝 | 拒绝 66 |
| 失去接口的抽象类之后才加载的具体子类 | 按新层级正确分派 | 持久发布前保护与后续结构重建共同保留正确分派，见[引擎文档](STRUCTURAL_REDEFINE_ENGINE.md#移除接口) |
| 有 Proxy 实现类的接口移除父接口 | 接受，已有代理不再是该父接口的实例 | 接受；共享父接口保留到最后一条继承路径被移除，已有引用沿用接口收窄规则 |
| 类种类变化 | 按方向和已有对象／子类型判断；部分成功返回后的缓存／GC 行为不安全 | 按[转换矩阵](CLASS_KIND_EVOLUTION.md)放行可验证的转换，保留必要的 62／66／70 边界 |
| 类有自身实例时改为 abstract | 接受，保留兼容字段与实例，缺实现调用抛 `AbstractMethodError`，禁止正常构造；有固定解释器/JIT/JNI 对照 | 仍拒绝 70：抽象类的独立 vtable／静态字段布局与 JIT/nterp 的嵌入分派假设冲突；GC 也按可实例化状态计算静态引用起点，见[阻塞分析与源码设计](LIVE_ABSTRACT_EVOLUTION.md) |
| boot 类、`Thread` 及其父类型、数组、代理类自身 | 部分可重定义 | 按 ART 的规则拒绝 |
| 新类（新 lambda、匿名类） | 由 HotswapAgent 等工具定义 | 由热重载工具追加到加载器，见[提供新类](#提供新类) |

接口收窄字段清理、静态复制及保留的 JNI／MethodHandle 调用路径各有独立规则与验证范围，见[引擎文档](STRUCTURAL_REDEFINE_ENGINE.md#移除接口)。DEX 的字段标识与 class_data 按规范顺序编码；冻结 D8 对只交换字段声明顺序的两个 ClassFile 生成逐字节相同的 DEX。结构事务无法观察这一输入差别，因此不宣称提供 JBR 的纯声明重排语义。

普通类实际移除接口时，已缓存的接口直接／精确绑定句柄抛 ICCE；未绑定的 `invokeWithArguments`／`asType` 在接收者转换时抛 CCE。缓存类句柄失去 default 时抛 AME，自身声明保留。API 33/36 的普通与 CheckJNI 回归另验错误 MethodType、null、方法体异常、两轮失败恢复、批次反序、晚加载与 GC。已绑定接口句柄的 `invokeWithArguments` 也在参数适配完成后抛 ICCE，回归覆盖数组／List、基本类型／引用／null 转换、错误参数个数、返回值适配、spread／drop 适配器与 ICCE 捕获。库通过自身的公开 JVMTI 环境保护 bootstrap `BindTo.transform` 入口，事务前必须实际证明活动帧可读。入口准备成功后在进程内保留，事务中止也不撤销。ART 为断点去优化该方法；旧 ART 在具名检查策略及二次覆盖探针通过后可清除自身断点，继续保留解释执行。探针可证明具名解释器服务时，Runnable 检查省去逐次活动帧读取与 JNI 字段访问；较新的 active-handler 解释器还能关闭自身逐指令通知。服务不可用时保留公开回调。定向解释执行与旧解释器的原生事件状态仍会增加成本；现代 arm64 叶入口保留 ART 的 DEX PC 展开信息并缩短其他解释调用的转发链，仍需连同普通调用、反射、JNI 和实际栈回溯一起衡量；接入应用需自行测量，其他位置的 JIT 保持启用。保留的接口及 copied default JNI ID 通过 CheckJNI 之后的分派保护处理：普通模式对失去接口的虚调用接收者抛 ICCE，nonvirtual default 保留方法体，CheckJNI 保留声明类／接收者诊断。指针／索引 ID、普通／V／A 与 virtual／nonvirtual 矩阵需独立进程和实际 ID 标签证明。普通声明方法的指针 ID 导致 ART 拒绝 79 时，只记安全拒绝，不能计作调用对齐。JNI 活动帧组合仍未完成。

接收者丢失接口的适配器矩阵扩展到 43 组预链接图，包括组合后绑定、替换／忽略接收者、guard 分支、处理器／过滤器顺序、collect／fold、invoker 与 varargs。不兼容捕获通过根化续执行交回 libcore，图中回调先于最终分派错误；这条少见路径需要 JNI 和栈读取，直接／兼容绑定保留已优化的原生路径。准备阶段验证混合参数、返回值与恰好一次调用。任意图、精确栈轨迹格式及 JNI 活动帧组合仍不在本证明范围内。

### API 33 的 obsolete 自身 private 调用

API 33 在真实运行时续接自证明通过后，另行开放普通类和接口的 virtual→private 变更。该能力独立于 `privateSelfSelectionClass/Interface`：`privateSelfContinuationPrepared` 表示命名服务准备完毕，`…Proven` 表示内部类／接口的真实重定义与旧帧调用通过（包括嵌套提交返回后旧调用方选择最新方法体），`…Active` 表示监听已开启，`…Kinds` 列出证明通过的目标种类；`…Failure` 说明不能准入的原因，`…EventPolicy` 为 `skip-move-result`。

服务缺失或自证明失败时，应用事务仍以 71 拒绝。API 30–32 不开放这条路径；API 36 继续使用命名选择入口。内部或应用事务一旦在续接保护下成功提交，监听在进程存续期间常驻，后续失败不撤销，覆盖提交前扫描之后新进入的旧帧。其他调试代理仍收到 invoke 事件，但被续接调用之后的 move-result 事件会跳过。局部变量、引用根、异常与非尾调用后续计算均留在原帧，应用 DEX 与访问标志不变。

其他调试代理强制 pop／retry 和异步异常投递尚无独立设备验收证据；相关分支保留 ART 的处理路径，不据此宣称所有调试代理组合均已对齐。

API 30–32 的续接适配目前仅为关闭准入的候选实现：已核对固定 Android 11／12／12L 源码与已存 ELF 名称，并加入 legacy 异常 ABI、DEX 方法身份和 signed root 服务。三版均没有本功能的设备证据，仍保留 71；内部 Debug 自证明也不能开放应用守卫。缺少 throwing direct access 入口的模块改用受限的精确 DexPc direct 调用适配，必须通过类／接口实际调用完成证明。API 33 可在 Debug 强制验证该路径，但不能据此宣称 API 30–32 已通过设备验收；不放宽共享成员访问检查，构造器不在适配范围。

新增诊断：`privateSelfContinuationDeviceValidated` 表示版本是否列入已验收的续接准入范围（当前仅 API 33），不能代替本进程 `…Proven`；`…ExceptionAbi` 为 `legacy-instrumentation`、`skip-flags` 或 `unavailable`；`…IdentityService` 为 `unverified`、`named-method-and-fixed-source` 或 `fixed-source`；`…DirectAccessProofs` 的位值 1／2 分别表示内部类／接口旧 private direct 帧真实通过，位值 8 另表示精确 DexPc direct 调用实际完成，旧观察位 4 不再产生。`…DirectBoundary` 报告选中的 `throwing-access`、`dex-pc-direct`、`modern-selector` 或 `unavailable`，与证明及准入相互独立；被续接的 direct 调用沿用跳过 move-result 调试事件的策略。


接口静态方法的公开枚举另经独立 `runStatic` 证明后对齐：应用子接口和实现类的 `getMethods()` 不再继承接口 static 声明；声明接口自身仍可枚举和调用，普通类的静态继承／隐藏不变。根因是 ART 收集所有 public 接口声明时没有排除 static，而固定 JBR 在合并父接口时明确排除。复用原枚举 Hook，仅在精确继承收集路径返回由真实 Method 组成的过滤数组，不改变方法本身或 `getDeclaredMethods()`。证明必须观察子接口、实现类、自身声明及空／混合过滤结果并实际调用；缺方法、全局根、部分观察或异常只关闭新扩展，已有最具体声明能力仍可启用。API 33／36 覆盖多层／菱形、类静态继承与隐藏、缓存调用、同批改体、接口方法变 static、失败事务、恢复与 GC。其他设备没有执行证明；API 30–32 续接仍保留 71。新增过滤性能未验收，旧性能和停顿问题不改标通过。


标准库接口 static 的继承视图另由独立 `runBootstrapStatic` 证明：应用子接口／实现类不再从 `Comparator`、`Function` 等 bootstrap 接口枚举继承来的 static 声明。该证明仅处理应用枚举根（标准库根另见下文独立证明）；标准库接口自身的 static 枚举／调用、普通声明枚举及 bootstrap 声明作为候选时的选择不变，仍以 ART 原生过滤后的 Method 数组为输入，不恢复被隐藏 API 规则排除的成员。证明必须观察应用接口与实现类上的实际过滤、bootstrap 自身视图的委托，并验证默认方法及自身静态方法的真实调用。Android 的公开 Class.getClassLoader 返回 BootClassLoader 而非 JBR 的 null，Java 证明按 Object.class 的加载器身份比较；native 使用公开 JVMTI 并要求独立观察。证明缺失／异常只关闭此扩展，应用接口 static 与桥接枚举继续可用。API 33／36 覆盖两个标准库接口、多层／双父接口顺序、应用自身 static、类 static 隐藏、默认方法、新建／缓存反射调用、改体／回滚／恢复与 GC。API 30–32 续接仍保留 71，性能越线和停顿仍未解决。


应用接口覆写 bootstrap 祖先的实例声明现在另经独立 `runBootstrapOverride` 证明：`getMethods()` 对完整签名保留应用子接口声明及其 bridge/synthetic/default 属性。固定 JBR 的最具体声明合并不按加载器区别祖先；ART 的父接口先收集与原守卫的 bootstrap 祖先跳过共同导致旧声明残留。证明要求接口／实现类的桥接／普通方法四路 native 观察及真实调用；不依赖 static 证明，缺失／部分观察／异常只关闭此扩展。标准库声明作为替换候选、标准库根的实例枚举、无关接口去重、类实现优先及 ART 原生过滤保持原策略。API 33／36 验证 Supplier、IntSupplier、Comparator、多层和两种菱形顺序、类覆写、缓存调用、改体／回滚／恢复与 GC。其他设备未验证；API 30–32 续接 71 不变，新增路径性能未单独验收，旧性能／停顿问题仍开放。


Bootstrap 接口作为最具体实例声明候选时，另经独立 `runBootstrapInstance` 证明后支持应用枚举根：List／SortedSet 的 default 和 Path 的桥接声明可替换其 bootstrap 祖先，保留真实 declaring Class 及 bridge/synthetic/default 属性。只使用 ART 已过滤并创建的 Method，不恢复隐藏成员，也不以 bootstrap 声明替换应用声明。标准库根的实例枚举、类实现优先、普通 declared 和无关接口去重继续保持原策略。证明须观察接口／实现类、桥接／普通方法四路成功替换并真实调用，缺失／部分观察／异常不启用；该证明不依赖旧 static 或应用覆写证明。API 33／36 覆盖集合默认方法、真实 Path 实现、缓存调用、改体／恢复／GC 和类型异常。其他设备仍未验证；API 30–32 续接 71 与原性能／停顿问题不变，新路径性能未单独验收。

标准库根的接口 static 继承视图另经 `runBootstrapRootStatic` 证明后修正：UnaryOperator 自身的 identity 保留，继承的 Function.identity 排除；Collator／RuleBasedCollator 不再枚举 Comparator 的 static 工厂，但普通类 static 的继承与默认方法调用保留。此前守卫在 bootstrap 根处提前委托，与固定 JBR 排除所有父接口 static 的规则不符。新证明依赖已通过的 bootstrap static 证明，独立观察接口根、类根及自身声明三路并真实调用；不依赖实例最具体声明证明，也不授权标准库根的实例替换。缺失、部分观察或异常仅关闭此扩展。原生可见性过滤、普通 declared 枚举、类 static 继承和无关接口去重保持原行为。API 33／36 验证新建／缓存调用、改体／失败／恢复、GC 与 null 异常；其他设备未验证，API 30–32 续接 71 不变。新增路径性能未单独验收，原性能越线和停顿问题仍开放。
