# 接口新增方法（设计）

状态：设计稿，尚未实现。实现合入后，本文内容并入 [STRUCTURAL_REDEFINE_ENGINE.md](STRUCTURAL_REDEFINE_ENGINE.md) 与 [LIMITATIONS.md](LIMITATIONS.md) 的中英文版本，本文删除。

## 目标

在现有接口方法体重定义（Android 13/API 33、Android 16/API 36，arm64）的基础上，允许接口目标：

1. 新增 default 方法（public、非 abstract 的实例方法）；
2. 新增 static 方法（任意访问级别，含 synthetic）与 private 实例方法；
3. 退役 static 方法与 private 实例方法（删除或改签名）。

第 3 项不在原始需求的字面范围内，但在接口默认方法里插入 lambda 时必然出现，见下文实测。仍不支持：新增 abstract 方法、删除或改签名 default/abstract 方法、字段变化、父接口变化、方法修饰符变化。

## 编译产物实测

样例：Kotlin 2.3.21 默认参数（`-jvm-default=enable`），`-jvm-target 1.8`；D8 8.10.9（build-tools 36.0.0），`--min-api 26` 与 `--min-api 33` 产物一致。接口 `Labels` 有 abstract `name()` 和 default `label()`，`label()` 内含一个捕获 `this` 的 lambda。

| 修改 | 接口 DEX 成员变化 | 其他类 |
| --- | --- | --- |
| 只改 lambda 体 | 无 | 已支持 |
| 在已有 lambda **之后**追加 lambda | 新增 `label$lambda$1`（private static）、新增 `$r8$lambda$<哈希>`（public static synthetic） | 新类 `Labels$$ExternalSyntheticLambda1` |
| 在已有 lambda **之前**插入 lambda | `label$lambda$0` 签名由 `(Labels)` 变为 `()`；旧 `$r8$lambda$<哈希>` 消失，新增两个 | `ExternalSyntheticLambda0` 字段与构造器签名变化；新增 `ExternalSyntheticLambda1` |
| 新增 Kotlin `private fun` | 新增 private 实例方法，经 `invoke-direct` 调用 | 无 |
| 新增带函数体的 `fun` | 新增 default 方法与 public static synthetic 桥 `access$<名称>$jd` | `Labels$DefaultImpls` 新增 static 方法；同模块实现类新增 `ACC_BRIDGE` 方法，以 `invoke-super` 调用接口 default |

结论：

- `$r8$lambda$` 的名称由实现方法的签名决定。只改 lambda 体时名称不变，插入或改变捕获时名称变化。
- 追加只产生新增；插入会让旧的 direct 方法消失，因此必须同时支持 direct 方法退役。
- 实现类的 bridge 和 `DefaultImpls` 都是普通类的新增，与接口放在同一批原子提交，现有批次能力已经覆盖。

## 为何复用现有接口重建组

接口方法体重定义已经为接口目标走 ART 结构分配：接口及其全部已加载子接口、实现类、实现类子类在同一事务内按新接口重新 `LinkClass`，实例由 ART 迁移，引用由 ART 改写，代码由 ART 去优化并失效（见引擎文档“接口事务的失败清理”一节）。新增方法不需要新的分配或提交路径：

- **接口自身**：新类按新 DEX 链接，方法数组、接口方法索引和 IMT 由 ClassLinker 重新计算。
- **实现类**：重新链接时 ClassLinker 为新 default 方法生成 copied 方法，或在类链上已有 public 实现时直接使用该实现。没有提交 DEX 的实现类仍使用原 DEX 和私有 DexCache，父接口经现有 `Redirect` 解析为替换类。
- **映射**：`PlanMethods` 以旧方法为起点按（名称、描述符）配对。新方法与实现类中新增的 copied 方法没有旧对应项，不进入映射，这与普通类新增方法相同。
- **调用点**：新方法只会被新代码引用。旧代码中经由已删除 direct 方法的解析项，按现有退役规则由 ART 的 visitor 清除，重新解析时抛 `NoSuchMethodError`。

这些路径是 ART 已经提供并由现有引擎驱动的，本设计不实现实例迁移、引用改写或失效。

## 校验规则变更（`structural_policy::Evaluate`）

接口目标的新增方法按 JVMTI 修饰符分类：

| 新增方法 | 结果 |
| --- | --- |
| static（任意访问级别），不是 `<clinit>` | 接受 |
| private 实例方法 | 接受 |
| public、非 abstract 实例方法（default） | 接受，另需满足下文两项冲突检查 |
| abstract 方法 | 拒绝 63：没有实现的已加载实现类会得到 miranda，调用时抛 `AbstractMethodError` |
| `<clinit>`、native 方法 | 拒绝 63：已初始化的接口不会执行新的 `<clinit>`；接口 native 方法没有注册路径 |

接口目标中旧方法消失时：

| 消失的方法 | 结果 |
| --- | --- |
| static 或 private 实例方法 | 可退役时接受，进入 `retired_methods`，与普通类的方法退役语义相同 |
| default 或 abstract 方法 | 仍拒绝 67 |

已有约束不变：

- 方法修饰符变化仍拒绝 71。改签名等于退役加新增，不算修饰符变化。
- 字段变化仍拒绝 64。
- 父接口变化仍拒绝 66。
- “已退役方法以另一种调用方式重新加入”仍拒绝 71。例如 `label$lambda$0(Labels)` 退役后，以实例方法身份重新加入同一名称和描述符时拒绝。
- 只要新增或退役了方法，`methods_structural` 就为真。接口目标本来就始终走结构路径，这一点只是保持一致。
- 只有自有成员映射可用时才接受，沿用接口目标现有的 `RequireMemberMapping`。

### 冲突检查一：新增 direct 方法不得遮蔽继承的接口方法

新增 static 或 private 方法与目标从父接口继承的虚方法同名同描述符时，拒绝 63。

- **原因**：已校验的 `invoke-interface I.m` 会在重新解析时找到这个 direct 方法，并按 IMT/iftable 分派，见引擎文档[调用方式不变](STRUCTURAL_REDEFINE_ENGINE.md#调用方式不变)。引擎无法让这条路径抛出 `IncompatibleClassChangeError`。
- **javac/kotlinc 的行为**：两者都不会为这种源码生成代码，这种情况只会出现在手写字节码或不一致的增量产物中。
- **快照新增事实**：`LoadedHierarchy` 新增目标父接口的虚方法集合。复用 `CollectInterfaceMethods(jni, target, ...)` 即可。接口的 `GetSuperclass` 为空，现有的 `CollectInheritedPublic` 拿不到这些方法。

### 冲突检查二：新增 default 不得改变已有调用的解析结果

已加载实现类或其父类链上，声明了同名同描述符的 static 方法或非 public 实例方法时，拒绝 63，并在原因里写出该类。

- **原因**：此时 ClassLinker 会把新 default 拷贝进实现类的方法数组。之后 `invoke-virtual C.m` 重新解析时，结果可能从父类的包内方法或 static 方法变成 copied default，已有调用因此改变语义，或触发调用方式不一致。
- **快照**：在 `CollectSubclasses` 遍历实现类时，顺着父类链用 `ReadLiveMethods` 收集这类身份，按类去重，并记录第一个声明它的类描述符。
- **不受影响的情况**：类链上有 public 实例方法（不论是否 abstract）时，类方法优先于 default，链接结果与修改前一致，可以接受。

### 冲突检查三：链接后不得新增 default 冲突

实现类同时从两个互不继承的接口获得同签名 default 时，ART 生成 default-conflict 方法，调用时抛 `IncompatibleClassChangeError`。新增 default 可能把一个原本正常的 copied default 变成冲突方法，让已有调用开始抛错。

这项检查放在 `PlanMethods`，读取 ART 自己的链接结果，不在引擎内重新实现 default 方法选择：

- 新类方法数组中满足 `IsDefaultConflicting` 的方法，即 `kAccIntrinsic | kAccCopied | kAccAbstract | kAccDefault` 这几位与 `kAccCopied | kAccAbstract | kAccDefault` 相等的方法，如果在旧类中没有同身份的冲突方法，则映射失败。失败返回 63，原因写明实现类与方法。
- 这时仍处于 `CollectAndCreateNewInstances` 之后、ART 不可失败点之前，现有清理路径负责回滚。
- 标志值取自固定源码声明：`kAccCopied = 0x01000000`、`kAccDefault = 0x00400000`、`kAccAbstract = 0x0400`，Android 13 与 16 相同（[13 modifiers.h](https://android.googlesource.com/platform/art/+/refs/tags/android-13.0.0_r1/libdexfile/dex/modifiers.h)、[16 modifiers.h](https://android.googlesource.com/platform/art/+/refs/tags/android-16.0.0_r1/libdexfile/dex/modifiers.h)；判定式见各版本 `art_method.h` 的 `IsDefaultConflicting`）。`kAccIntrinsic = 0x80000000` 同源。这些常量放在 `platform/abi/arm64/common`，不新增任何对象偏移，`access_flags` 已经由现有布局读取。
- 方法数组按 `PlanMethods` 现有的 `Members` 与长度遍历，不以数组指针非空推断存在成员。较新的 ART 模块已对无字段的类使用地址非空、长度为零的共享字段数组（见引擎文档“运行时探测”），方法数组按同一假设处理。
- 这项检查对所有结构事务生效，包括“给普通类新增接口”。那条路径今天也可能产生同样的冲突，属于同一个根因，不另加一套接口专用分支。

以下情况都接受：

- 新 default 覆盖父接口的 default：更具体的接口胜出，是 Java 的正常语义，旧的 copied 方法按身份映射到新的 copied 方法。
- 子接口已经声明同签名 default：子接口更具体，结果不变。
- 抽象实现类原来的 miranda 变成 default 拷贝：原本抛 `AbstractMethodError` 的调用变为可执行。

## 直接方法退役

接口中 direct 方法的退役复用现有退役机制，不新增守卫：

- 映射为 `{old, old, kRetired}`，提交时由 `RetirementVisitor` 清除 DexCache 解析项，并记录 JNI ID。
- JNI `CallStatic*`/`CallNonvirtual*`、`Method.invoke` 和 MethodHandle 的守卫与声明类无关，同样适用于接口。
- 退役记录进入按类保存的 `RetiredMethods`，供之后的事务检查调用方式。
- 活动旧帧继续执行旧方法体。

需要逐项核对的点：退役保活的 JNI 全局根要包含接口类（实现中确认 `RetainRoots` 的类集合）；`hidden_natives` 不涉及接口。

## 语义与代价

- 已有实例立即获得新 default 方法。新 static/private 方法只被新代码调用。
- 旧 lambda 实例：D8 为 lambda 分配的序号在插入后发生偏移，旧实例所属的合成类被重定义成另一个 lambda 的实现，或因实现的函数接口变化而以 66 拒绝。这和普通类里插入 lambda 是同一个问题，不属于引擎语义。热重载端需要像处理普通类那样隔离 lambda。引擎支持新增接口 static 方法以后，热重载端可以保留旧的 `$r8$lambda$` 桥，只追加新桥。
- 代价与接口方法体修改相同：重新分配所有实现类及其子类的实例，随实例数量和堆大小增长。

## 能力报告

不新增能力字段。`interfaceRedefinition` 已经表示接口运行时探测状态；成员演进是否被接受，由每次事务的策略决定，结果通过返回码和 `lastRejection` 给出。文档中的错误码表随实现更新。

## 验证计划

主机侧：

- `tools/test-structural-policy-host.py` 补充以下用例：
  - 新增的三类方法分别接受；abstract、`<clinit>`、native 拒绝。
  - direct 方法退役接受；default/abstract 删除拒绝。
  - 遮蔽继承的接口方法拒绝；实现类链上有非 public 或 static 同签名方法时拒绝，有 public 实现时接受。
  - 退役后以另一种调用方式重新加入时拒绝。

设备侧（API 33 与 API 36，普通模式与 CheckJNI 模式）：

- API 36 须同时覆盖使用共享空字段数组的 ART 模块（Android 16 QPR2）。本设计的 fixture 接口都没有字段，这类设备上的接口准入依赖成员探测接受共享空数组（提交 `a8a427a3`）。

- 新增 fixture 变体，用 javac 编译的 lambda 产生 `$r8$lambda$` 桥：
  - 追加 default、追加 private static、追加 private 实例方法。
  - 在 default 方法里追加 lambda、在已有 lambda 之前插入 lambda（同批重定义合成 lambda 类并新增类）。
  - 接口与带 bridge 的实现类同批提交。
  - 没有提交 DEX 的实现类获得 copied default。
  - 子接口覆盖、miranda 变为 default 拷贝。
  - 菱形结构中新增 default 引发冲突时拒绝，并且旧调用保持可用。
- 每个场景都核对：已有实例的新旧分派、退役方法经字节码/JNI/反射/MethodHandle 访问时抛 `NoSuchMethodError`、GC 后行为不变、`lastRejection` 文本正确。
- `InterfaceAbortInstrumentation` 补充一项：冲突检查失败后，旧定义完整保留，随后父类事务与接口事务都能成功。
- `interface-stress` 增加新增 default 的版本，核对 200 个实现类的 copied 方法与 GC 后行为。

## 实施顺序

前置：提交 `a8a427a3`（成员探测接受共享空字段数组）先合入 main，后续步骤以它为基线。

1. 策略与主机测试：修改 `structural_policy` 和 `LoadedHierarchy`，纯函数，可以独立提交。
2. 快照事实：在 `structural_redefine_engine.cc` 收集父接口虚方法和实现类链上的 static/非 public 身份。
3. 链接后冲突检查：在 `method_retirement.cc` 的 `PlanMethods` 中实现，并声明 flag 常量。
4. 接口 direct 方法退役：放开策略，核对保活与守卫。
5. fixture、设备套件与失败仪器。
6. 文档：中英文版本的 LIMITATIONS、引擎文档、README，并删除本文。

每一步单独验证并提交。第 1、2 步只影响策略输入，不改 ART 交互。第 3 步是唯一新增读取 ART 运行时标志的地方。
