# Class redefine 未支持与未充分验证的能力

记录时间：2026-10-08。已适配目标设备：`DEVICE_001`，Android 13 / API 33 / arm64。当前是默认 NDK 单增强库与命名设备函数/自有 Hook 代理架构；独立源码引擎、AOSP 构建与 `-PsourceEngine13` 已删除。命名 ELF 符号、固定源码声明与公开 JNI/JVMTI 自有行为校准是私有演进的启用条件；Build ID 仅记录来源，不参与能力匹配。

最终无源码 APK `1c532b1f…` 已通过 39 个 Host runner、41 项真机一致性检查、标准入口六组、两种 Hook 顺序的完整套件及相关专项；迁移验收完成，完整哈希、命令和逐次证据以 [VERIFICATION.md](VERIFICATION.md) 为准。原有能力限制保持不变。此处能力限制不会因构建迁移自动消失。

迁移前完整源码演进 APK `f3087a3ae14d68c72dcf8d5e4dfc6092a0089090e1573c5a8ac86a33facbab5b` 曾在两种 Hook 顺序下各完成 31 次结构提交和 135 项修饰符检查：[validation-first](../build/evidence/source-engine/20261006T153859Z/run.json)、[source-first](../build/evidence/source-engine/20261006T154308Z/run.json)。下面引用的旧专项是历史语义证据，不能作为最终无源码 APK 的完整长期验收，也不代表任意类变更均已实现。

## 明确不支持或会拒绝的情况

| 情况 | 当前行为 | 要改变现状需要什么 |
| --- | --- | --- |
| 非调试 AOT **活动机器码帧**在暂停点立即转换成解释器帧 | 对无法确认可安全去优化的帧返回 66；旧定义保持完整，帧退出后可重试。两种 Hook 顺序的拒绝、旧帧返回和重试已验证：[结果一](../build/evidence/source-engine/20261006T091127Z/probe.json)、[结果二](../build/evidence/source-engine/20261006T091149Z/probe.json)。 | 证明目标编译策略的栈图可恢复暂停点的 Dex 寄存器，再验证实际切换、内联及并发帧；不能只放开预检。详见 [TODO.md](TODO.md)。 |
| 添加方法 `final`，但已有加载的子类覆写它 | 返回 71，事务不提交。这是已验证的冲突拒绝，不应强行提交。[专项](../build/evidence/source-engine/20261006T093015Z/probe.json) | 若需支持，必须定义并实现现存子类及旧调用点的一致语义。 |
| 添加类 `final`，但已有加载的子类 | 返回 70，事务不提交。这是类关系冲突。[能力说明](README.md#能力与版本选择) | 同上，不能仅改修饰符位。 |
| 类或方法变为 abstract 后，存活的直属实例或已加载子类将继承未实现的抽象方法 | 事务返回 71，旧定义不变。[能力说明](README.md#能力与版本选择) | 需要先消除依赖，或设计安全的对象迁移和分派语义。 |
| 缺少命名符号或未通过自有行为校准的 ART 构建 | 完整私有演进初始化拒绝；公共 JVMTI 可进行标准方法体替换，ART 暴露结构扩展时可新增成员。当前开发批次已将生产 backend_factory 改为按独立 bootstrap 证明选择能力；完整源路径仍要求其全部独立前提成立。该组合批次尚待统一 APK 回归，Android 11 正式方法删除子集的证据不能证明 Android 11–17 完整事务均已接入。[验证记录](VERIFICATION.md) | 以实际符号原型、固定 ABI 声明和自有行为证据选择能力，并接入各版本真实结构事务；不能仅去掉 SDK 判断后套用 Android 13 声明，也不能新增 Build ID 白名单。 |
| 完整演进初始化失败或尚未初始化时的 `EVOLVE_SCHEMA` | 明确返回不可用，不自动退化成较弱的重定义。[公共 API](README.md#能力与版本选择) | 在已适配设备调用 `DcevmRuntime.initialize(context)` 并处理初始化错误；内部诊断使用 `enableEvolution()`。不需要源码构建属性。 |

## 已有部分实现，但仍缺关键验证

| 能力或风险 | 已有证据与缺口 | 完成判据 |
| --- | --- | --- |
| 标准 `RedefineClasses` 的转换回调与多环境并发 | 独立 Agent 的 `ClassFileLoadHook` 把方法体补丁改成新增字段补丁，以及把原定义补丁改成移除父类补丁，两个 Agent 并发修改不同类，均在两种加载顺序下通过；其他结构转换、同类并发和回调组合尚未验证。[设备结果](../build/evidence/standard-jvmti/20261006T153842Z/result.json) | 覆盖转换后的接口及成员删除、同类竞争与多个回调，并确认最终 DEX 的守卫、错误码和事务原子性。 |
| boot JIT 帧内联应用方法，且暂停时尚未执行到内联点 | 预检已有保守扫描；`ArrayList.forEach` → 应用回调实验没有产生目标内联帧，目标 `HGraph::InlineInto` 命中 0 次。[诊断](../build/evidence/source-engine/20261006T084146Z/probe.json) | 构造真实目标帧，在两种 Hook 顺序下证明继续执行失效指令前已切换，或旧定义未改动时安全拒绝。按用户要求暂缓，详见 [TODO.md](TODO.md)。 |
| 更广的 AOT/JIT 编译调用形态 | 已验证部分应用 AOT 活动帧、JIT 内联帧和两个并发内联帧；优化内联、更多并发帧、不同调用形式混合以及长期编译策略未穷尽。非调试 AOT 即时 DeOPT 仍不支持。[编译执行记录](README.md) | 为每类目标形态取得实际机器码栈、提交结果及恢复后返回值，并覆盖双 Hook 顺序。 |
| 接口移除和旧父类移除的全部语义 | 已测接口默认方法、缓存特殊句柄、三代关系、部分 AOT 与批次；其他默认方法、缓存接口引用、较大批次和优化调用形态未穷尽。总括标志 `hierarchyChange=false`。[当前状态](../build/evidence/status.json) | 扩大调用与缓存矩阵，验证新旧类型关系、异常、对象状态和无效批次原子性。 |
| 成员修饰符的全部组合与旧权限缓存 | 单项转换和若干组合已测；其他组合、旧解析访问权限收窄的全部调用形式尚未验证。总括标志 `memberModifierChange=false`。[当前状态](../build/evidence/status.json) | 对合法组合及旧字节码、反射、JNI、MethodHandle、VarHandle 和 JIT 调用逐项验证。 |
| 存活直属实例参与类 `abstract` 切换时的优化执行 | 当前使用持续到进程结束的进程级解释执行回退；语义专项已测，但尚未恢复优化执行。[能力说明](README.md#能力与版本选择) | 在旧实例、子类、GC 和编译调用同时存在时实现并验证安全的有限范围回退或恢复策略。 |
| 元数据完整回收与长期 ClassLoader 生命周期 | 两种 Hook 顺序各通过 256 轮混合字段/方法退役、后台 GC 和类加载器卸载；其他缓存引用形式及更长时间仍未覆盖。总括标志 `metadataReclamation=false`。[证据一](../build/evidence/source-engine/20261006T044759Z/probe.json)、[证据二](../build/evidence/source-engine/20261006T044857Z/probe.json) | 扩展旧反射、JNI 与句柄保活/释放矩阵，做长期内存与卸载验证。 |
| 长期运行及前台交互下的稳定性 | 旧 APK 有前台绘制和 1000 次提交证据，也有过 ART 恢复路径崩溃；历史 APK 的完整 class redefine 回归已通过，最终无源码 APK 仍须记录验收，但尚未完成对应时长的实体屏幕验收。[历史记录](README.md) | 用当前 APK 做长时结构提交、实体屏幕绘制、焦点变化和多轮独立启动验证，记录崩溃与资源趋势。 |
| 失效接收者的绑定 `MethodHandle` 异常语义 | ART 在已测形态会抛 `IllegalStateException`；是否应统一为其他链接异常尚需审查。[现有说明](README.md) | 对照目标语义，覆盖新旧句柄和 JIT 调用后决定是否修改。 |

## 使用这些记录的原则

- `capabilities()` 中单项为 `true` 只证明该项已测范围；总括的 `memberModifierChange`、`hierarchyChange`、`metadataReclamation` 和 `fullDcevmComplete` 当前均为 `false`。[状态文件](../build/evidence/status.json)
- 返回 66、70、71 等拒绝结果时，应把本次替换视为**未提交**，不要假定新定义部分生效；依照具体冲突消除原因或等待活动帧退出再重试。
- 后续完成一项时，应同时更新这里的现状、目标设备证据和总括能力标志；旧 APK 的专项通过不能自动证明新 APK 的长期稳定性。
