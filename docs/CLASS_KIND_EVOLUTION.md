# 类种类演进

固定参照为 JBR 21.0.11+10-b1163.116，源码修订 `cf1f197170a0f04c6559feb873e362583be021d1`。
`tools/compare-jbr-class-kinds.py` 在每个独立 VM 中测量转换、已有实例、已加载子类型、字段、旧 virtual/interface 调用、反射、MethodHandle、类型检查、两轮转换和失败批次。原始输出仅存放在忽略的 `build/evidence/`。

## 转换矩阵

表中的普通类默认以 Object 为父类，枚举默认以 Enum 为父类。种类与父类必须分别判断；“清除 enum 位但保留 Enum 父类”与“把枚举改成 extends Object 的普通类”是不同转换。

| 原种类 → 新种类 | 固定 JBR 的实测前提 | 本引擎的准入条件 |
| --- | --- | --- |
| 普通类 → 接口 / 注解 | 无已加载子类时重定义成功，包括已有直接实例；旧 invokevirtual 抛精确 ICCE，反射调用已有实例也抛 ICCE；缓存 MethodHandle 的返回值不稳定 | 无已加载子类型、无直接实例、已取得 Native 阶段的活动帧快照且批次目标无活动帧，新 DEX 不引用自身数组；必须有已探测的符号调用检查 |
| 接口 / 注解 → 普通类 | 无已加载实现类或子接口时成功；旧 invokeinterface 抛精确 ICCE，包括 null 接收者 | 同样要求无已加载子类型、已取得 Native 阶段的活动帧快照且批次目标无活动帧，新 DEX 不引用自身数组，交给 ART 重新链接普通类布局 |
| 接口 ↔ 注解 | 可保留已加载实现类和实例；实例、缓存反射及 MethodHandle 的调用保持实现类结果 | 保留接口组重建、成员映射和原有父接口增删检查；Annotation 父接口随最终定义演进 |
| 普通类 / 接口 / 注解 → 枚举 | 无已加载子类型时成功，新增 Enum 父类；不重新运行初始化器，新枚举常量保持默认 null；普通类的已有实例在后续 GC 可使固定 JBR 崩溃 | 新增 Enum 父类仍以 66 拒绝：ART 缺少新自类型数组身份图，libcore 对未初始化常量表的反射行为也不同；保留原 Enum 父类的转换另行允许 |
| 枚举 → 普通类，移除 Enum 父类 | JBR 拒绝父类缩窄 | 66，保留原定义 |
| 枚举 → 接口 / 注解 | JBR 拒绝移除 Enum 父类 | 66，保留原定义 |
| 枚举 ↔ 普通类，保留 Enum 父类 | 可保留枚举常量、字段布局、缓存反射、MethodHandle 和成员调用 | 允许，沿用 ART 事务的对象与成员引用改写；不手工改已发布 Class 的标志 |

准入使用 Native 已加载类型集合，包括单独由 Proxy 维护、未进入 ART 普通重建组的代理类；它们同样阻止接口变成普通类。上述跨 class/interface 转换存在已加载子类型时，单目标探针在 JBR 抛 VerifyError。同步更新 Target 与 Impl 的四个配对探针在该固定 JBR 均被拒绝为 InternalError，未据此开放本引擎的配对子类型转换。当前实现对子类型不变的跨界转换返回 62。ClassFileLoadHook 才引入、Native 阶段未预测的跨界变化，若没有活动帧快照则以 70 拒绝。跨界转换的新 DEX 引用自身数组时以 66 拒绝，避免把已发布数组的旧组件与新类混用；检查覆盖只出现在方法代码中的 type_id，不依赖字段／方法签名。普通类转接口的已有实例返回 70；这是 ART 分配契约及固定 JBR 缓存行为所决定的安全边界，不能宣称该方向已完全对齐。

## ART 的限制与决策点

`CheckClass` 原生逐位比较 ClassDef 修饰符；增强策略此前又统一以 70 拒绝 interface、annotation 和 enum 变化。放开位比较无法处理以下差异：

- `AllocateNewClassObject` 必须从新 DEX 的 ClassDef 建立新 Class。`LinkClass` / `LinkMethods` 根据新种类重算 virtual/interface 方法索引、IfTable、IMT、静态字段起点及 Class 大小，必要时通过 `Class::CopyOf` 重分配含嵌入 vtable 的类对象。
- `LinkSuperClass` 要求接口以 Object 为父类、普通类不得以接口为父类。保持旧子类或旧实现类的声明会违反这项契约，不能把它们原样重建。
- `CollectAndCreateNewInstances` 为旧实例调用 `Class::Alloc`；其声明要求目标可实例化。普通类变为接口时不能先分配接口实例再检查。新增 Enum 父类还引入 name/ordinal 字段、新自类型数组及未初始化常量表，当前不能证明完整语义。
- 相同方法名和签名不代表相同调用约定。旧 virtual 方法索引不能进入接口布局，旧 interface 方法索引也不能进入普通类 vtable。

策略为种类变化强制走结构路径并要求自有成员映射。只有 enum 位变化且层级、字段和方法声明完全相同时，完整验证新代码可沿用已发布的自类型／数组身份图：在 ART 的私有 holder 临时选择旧 Class，替换 Class 始终由 NewClasses 根保活，校验返回即恢复。声明变化时不适用此路径，也不跳过任何输入代码校验。原有父类 handle 决策把新父类传给 ART。跨 class/interface 时扫描调用方并沿用解释执行保护；调用守卫从调用方自己的 DEX（包括 obsolete DEX）取得 method_id 的符号类型，再经命名 `DoLookupResolvedType` 检查当前种类。不能用解析后方法的声明类替代符号类型：普通类可继承接口默认方法，接口调用也可解析到 Object 方法。

在原生 ART 已暂停类加载与分配、尚未分配替换实例的 Collect 决策点，使用公开 JVMTI `IterateThroughHeap` 的精确类过滤检查直接实例。独立 JVMTI 环境需取得 can_tag_objects，但回调不读写 tag、不调用 JNI、不分配对象。能力缺失就拒绝需要这项前置检查的转换。原 ART 仍负责替换实例、字段复制、根和反射引用改写、去优化以及缓存失效。

缓存构造器在当前类型不可实例化时先检查分配条件：旧 `new` 抛 InstantiationError，缓存 MethodHandle、JNI NewObject 和 libcore 反射抛 InstantiationException；恢复普通类后，已删除的旧构造器仍抛 NoSuchMethodError，反射按 libcore 规则包装为 InvocationTargetException。固定 JBR 的 Java 反射访问器另有历史相关行为：转换前已调用的 Constructor 在接口状态包装 InstantiationException，恢复后包装 NoSuchMethodError；首次在接口状态调用的 Constructor 会缓存失败访问器，恢复后仍直接抛 InstantiationException。独立的冷／热构造器探针保留这些精确结果。本引擎沿用 libcore 的反射与缓存机制，没有模拟 JDK 反射访问器；这是已验证、尚未对齐的库层差异。

## 验证

- 固定 JBR：`python3 tools/compare-jbr-class-kinds.py --java-home <fixed-jbr>`；可显式添加 `--include-unsafe-live-enum`，在独立进程中复核已知 GC 失败。该选项可能生成仅存于忽略证据目录的 VM 崩溃日志。
- 设备：`tools/run-device-tests.py --scenario-group class-kinds`，覆盖 class/interface/annotation 双向重复转换、旧调用与 null 接收者的精确异常、instanceof/checkcast、缓存反射/MethodHandle/JNI ID、枚举字段和常量、存活实例与子类型拒绝、分配后整批回滚及恢复。
- 主机：结构策略、真实命名调用守卫和固定声明、类文件配方、安装与回滚契约；完整设备套件继续验证原有功能。

设备及 APK 哈希和逐次验证命令只记录在忽略的 `local/verification/` 中。此矩阵不声称支持带已有实例的普通类转接口、新增 Enum 父类或移除 Enum 父类；跨 class/interface 的自身数组同样保留 66。这些是本次明确保留的 ART 差距。
