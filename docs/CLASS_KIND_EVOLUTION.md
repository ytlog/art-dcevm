# 类种类演进

固定参照为 JBR 21.0.11+10-b1163.116，源码修订 `cf1f197170a0f04c6559feb873e362583be021d1`。
`tools/compare-jbr-class-kinds.py` 在每个独立 VM 中测量转换、已有实例、已加载子类型、字段、旧 virtual/interface 调用、反射、MethodHandle、类型检查、两轮转换和失败批次。原始输出仅存放在忽略的 `build/evidence/`。

## 转换矩阵

表中的普通类默认以 Object 为父类，枚举默认以 Enum 为父类。种类与父类必须分别判断；“清除 enum 位但保留 Enum 父类”与“把枚举改成 extends Object 的普通类”是不同转换。

| 原种类 → 新种类 | 固定 JBR 的实测前提 | 本引擎的准入条件 |
| --- | --- | --- |
| 普通类 → 接口 / 注解 | 无已加载子类时重定义成功，包括已有直接实例；旧 invokevirtual 抛精确 ICCE，反射调用已有实例也抛 ICCE；缓存 MethodHandle 的返回值不稳定 | 无已加载子类型、无直接实例、已取得 Native 阶段的活动帧快照且批次目标无活动帧，自身数组须有命名验证器组件身份服务；必须有已探测的符号调用检查 |
| 接口 / 注解 → 普通类 | 无已加载实现类或子接口时成功；旧 invokeinterface 抛精确 ICCE，包括 null 接收者 | 同样要求无已加载子类型、已取得 Native 阶段的活动帧快照且批次目标无活动帧，自身数组须有命名验证器组件身份服务，交给 ART 重新链接普通类布局 |
| 接口 ↔ 注解 | 可保留已加载实现类和实例；实例、缓存反射及 MethodHandle 的调用保持实现类结果 | 保留接口组重建、成员映射和原有父接口增删检查；Annotation 父接口随最终定义演进 |
| 普通类 / 接口 / 注解 → 枚举 | 无已加载子类型时成功，新增 Enum 父类；不重新运行初始化器，新枚举常量保持默认 null；普通类的已有实例在后续 GC 可使固定 JBR 崩溃 | 新增 Enum 父类仍以 66 拒绝：新增 Enum 布局尚未开放，libcore 对未初始化常量表的反射行为也不同；保留原 Enum 父类的转换另行允许 |
| 枚举 → 普通类，移除 Enum 父类 | JBR 拒绝父类缩窄 | 66，保留原定义 |
| 枚举 → 接口 / 注解 | JBR 拒绝移除 Enum 父类 | 66，保留原定义 |
| 枚举 ↔ 普通类，保留 Enum 父类 | 可保留枚举常量、字段布局、缓存反射、MethodHandle 和成员调用 | 允许，沿用 ART 事务的对象与成员引用改写；不手工改已发布 Class 的标志 |

准入使用 Native 已加载类型集合，包括单独由 Proxy 维护、未进入 ART 普通重建组的代理类；它们同样阻止接口变成普通类。上述跨 class/interface 转换存在已加载子类型时，单目标探针在 JBR 抛 VerifyError。同步更新 Target 与 Impl 的四个配对探针在该固定 JBR 均被拒绝为 InternalError，未据此开放本引擎的配对子类型转换。当前实现对子类型不变的跨界转换返回 62。ClassFileLoadHook 才引入、Native 阶段未预测的跨界变化，若没有活动帧快照则以 70 拒绝。跨界转换允许新 DEX 引用自身数组；缺少命名验证器组件身份服务时仍以 66 拒绝。检查覆盖只出现在方法代码中的 type_id，不依赖字段／方法签名。普通类转接口的已有实例返回 70；这是 ART 分配契约及固定 JBR 缓存行为所决定的安全边界，不能宣称该方向已完全对齐。

## ART 的限制与决策点

`CheckClass` 原生逐位比较 ClassDef 修饰符；增强策略此前又统一以 70 拒绝 interface、annotation 和 enum 变化。放开位比较无法处理以下差异：

- `AllocateNewClassObject` 必须从新 DEX 的 ClassDef 建立新 Class。`LinkClass` / `LinkMethods` 根据新种类重算 virtual/interface 方法索引、IfTable、IMT、静态字段起点及 Class 大小，必要时通过 `Class::CopyOf` 重分配含嵌入 vtable 的类对象。
- `LinkSuperClass` 要求接口以 Object 为父类、普通类不得以接口为父类。保持旧子类或旧实现类的声明会违反这项契约，不能把它们原样重建。
- `CollectAndCreateNewInstances` 为旧实例调用 `Class::Alloc`；其声明要求目标可实例化。普通类变为接口时不能先分配接口实例再检查。新增 Enum 父类还引入 name/ordinal 字段、新自类型数组及未初始化常量表，当前不能证明完整语义。
- 相同方法名和签名不代表相同调用约定。旧 virtual 方法索引不能进入接口布局，旧 interface 方法索引也不能进入普通类 vtable。

策略为种类变化强制走结构路径并要求自有成员映射。只有 enum 位变化且层级、字段和方法声明完全相同时，完整验证新代码可沿用已发布的自类型／数组身份图：在 ART 的私有 holder 临时选择旧 Class，替换 Class 始终由 NewClasses 根保活，校验返回即恢复。声明变化时不适用此路径，也不跳过任何输入代码校验。原有父类 handle 决策把新父类传给 ART。跨 class/interface 时扫描调用方并沿用解释执行保护；调用守卫从调用方自己的 DEX（包括 obsolete DEX）取得 method_id 的符号类型，再经命名 `DoLookupResolvedType` 检查当前种类。不能用解析后方法的声明类替代符号类型：普通类可继承接口默认方法，接口调用也可解析到 Object 方法。

自类型数组保留 ART 的已发布 Class 身份，包括验证时首次加载的一至多维数组。整个数组查找期间暂停事务的描述符重定向，使递归组件解析仍指向已发布类；这避免把私有替换类的数组发布到 ClassTable 或写入 DexCache。验证器分解数组时，命名 `RegTypeCache` 服务把旧组件按 ART 已有 OldClasses/NewClasses 根映射到替换类。ART 11–15 的 `FindClass`/`InsertClass` 成对使用，并按替换类的 final 标志重算组件精确度；ART 16/17 使用 `FromClass`。验证继续执行原 ART 代码，失败仍拒绝整批。提交时由原生 `ReplaceReferences` 更新已发布数组的组件；增强库不分配替代数组镜像、不改共享组件字段、不重写全局引用。

`self-arrays` 设备组使用与固定 JBR 相同的 `SelfArrayProbe`，覆盖 final 普通类 ↔ 接口、冷／热数组、一至三维组件、保活数组、字段和方法返回类型、缓存反射及 MethodHandle、数组读写／强制转换的精确异常、两轮恢复、GC、验证失败和映射规划失败后的恢复。数组中只保留 null 或下一级数组，没有直接目标实例；另外创建直接实例的拒绝场景仍要求 70，单独计数。此次验收不同时变更父类或父接口；数组与这些层级变更叠加的组合仍需单独验证。

固定 JBR 的同名双加载器探针暴露了独立边界：仅转换一个加载器的目标时，另一个加载器的数组常量、matrix/cube 和 cast 可错误解析到被转换的类型；含坏类文件的同名双加载器失败批次还可能污染下一次恢复后的转换。`compare-jbr-self-arrays.py` 将这些结果记入 `knownOracleDifferences`，与 Android 保持加载器隔离、失败回滚及恢复的预期分开核对，不把这些差异或安全拒绝计作调用对齐。

在原生 ART 已暂停类加载与分配、尚未分配替换实例的 Collect 决策点，使用公开 JVMTI `IterateThroughHeap` 的精确类过滤检查直接实例。独立 JVMTI 环境需取得 can_tag_objects，但回调不读写 tag、不调用 JNI、不分配对象。能力缺失就拒绝需要这项前置检查的转换。原 ART 仍负责替换实例、字段复制、根和反射引用改写、去优化以及缓存失效。

缓存构造器的分配与调用身份分别处理。旧 `new` 在当前类不可实例化时抛 `InstantiationError`，缓存 MethodHandle 和 JNI `NewObject` 抛 `InstantiationException`；恢复普通类后，已删除的旧构造器仍抛 `NoSuchMethodError`。

Java 反射另保留固定 JBR 的访问器历史。公有查询（`getConstructor` / `getConstructors`）与声明查询（`getDeclaredConstructor` / `getDeclaredConstructors`）使用不同的共享根。同一根的副本即使在首次调用前取得，也会共享之后创建的访问器；参数个数或类型错误同样会初始化可调用访问器。已有可调用访问器在不可实例化状态先检查参数，再包装 `InstantiationException`；仅改 abstract 并恢复后可继续调用，构造器已退役则包装 `NoSuchMethodError`。冷副本首次在不可实例化状态调用时缓存失败访问器，不检查参数；恢复为普通类后仍直接抛 `InstantiationException`。失败事务不重置这些历史，成功事务中的新查询使用新根。

`constructor_history` 使用独立公开 JVMTI 标签保存共享根与副本身份，`ObjectFree` 释放本机记录，不强引用构造器或加载器。单个查询只为已由命名栈遍历器确认的 bootstrap Class 帧请求 JVMTI 方法 ID，避免在指针模式下给应用调用方新增历史 pointer-id 标记。已有 ART 事务仍更新真实成员与类引用。重复调用通过本线程最近一次构造器的 JNI 弱引用比较身份，由 ART 负责移动与清除引用；线程结束时删除弱引用并释放本机共享根。优先使用经过相同／不同对象探针验证的命名标准 JNI `IsSameObject`，服务缺失时使用公开 JNI 表；不替换调用方的 CheckJNI 表。`constructorHistoryCacheProofs` 的位 0/1 分别表示标签元数据与命名弱引用身份探针；这只是诊断，不能作为事务准入判断。

范围仍有限：初始化库之前取得、直接调用内部发现入口或用于序列化的构造器不在共享根证明中；并发构造活动帧、全部非 public 访问组合，以及 JBR 软引用反射缓存回收后的新查询共享关系也尚未闭合。旧构造器在退役并恢复后才首次调用的独立 JBR 探针会抛 `ClassCastException`，当前保留安全的退役拒绝，另记差异，不能计作已对齐。带已有直接实例的 concrete→abstract 仍按原有 70 边界处理。

## 验证

- 固定 JBR：`python3 tools/compare-jbr-class-kinds.py --java-home <fixed-jbr>`；可显式添加 `--include-unsafe-live-enum`，在独立进程中复核已知 GC 失败。该选项可能生成仅存于忽略证据目录的 VM 崩溃日志。
- 设备：`tools/run-device-tests.py --scenario-group class-kinds`，覆盖 class/interface/annotation 双向重复转换、旧调用与 null 接收者的精确异常、instanceof/checkcast、缓存反射/MethodHandle/JNI ID、枚举字段和常量、存活实例与子类型拒绝、分配后整批回滚及恢复。
- 历史访问器：`python3 tools/compare-jbr-constructor-history.py --java-home <fixed-jbr>` 使用与设备相同的 Java 探针和独立固定预期；设备 `--scenario-group constructor-history` 覆盖公有／声明根双向独立的冷／热与错误参数预热、副本与数组、两轮转换恢复、GC 和规划后失败。主机 `test-constructor-history-host.py` 另验生产守卫、原始类型拓宽、标签回收、弱引用清除／分配失败、线程退出清理与安装重试。
- 自类型数组：`python3 tools/compare-jbr-self-arrays.py --java-home <fixed-jbr>`；设备 `--scenario-group self-arrays`。`test-verification-redirect-host.py` 验证生产重定向的作用域、加载器身份、精确度、服务缺失及原验证结果传递；`test-apk-verification.py` 拒绝缺失数组行为或擅改异常的报告。
- 主机：结构策略、真实命名调用守卫和固定声明、类文件配方、安装与回滚契约；完整设备套件继续验证原有功能。

设备及 APK 哈希和逐次验证命令只记录在忽略的 `local/verification/` 中。此矩阵不声称支持带已有实例的普通类转接口、新增 Enum 父类或移除 Enum 父类。普通类／接口自类型数组已在上述前提下开放；服务缺失保留 66。原始指针 JNI ID 启动配置仍可能被 ART 的结构重定义准入以 79 拒绝，这些结果单列为未对齐，不计作数组调用成功。JBR 的跨加载器和失败批次边界另行记录，不声称该组合完全对齐。
