# Class redefine 未支持与未充分验证的能力

记录时间：2026-10-06。目标设备：`DEVICE_001`，Android 13 / API 33 / arm64。当前已验证 APK SHA-256：`4fe0ac9f477a4755b445b1e84696293d866b783f67e60743c5da00cd17aafba9`。

本清单只讨论虚拟机的 class redefine / schema evolution。当前 APK 在两种 Hook 顺序下通过完整回归，各完成 31 次结构提交和 135 项修饰符检查：[validation-first](evidence/source-engine/20261006T104741Z/probe.json)、[source-first](evidence/source-engine/20261006T105256Z/probe.json)。这支持“当前设备上开发阶段初步可用”的判断，不代表任意类变更都已实现或验证。以下项目中的历史专项可能来自旧 APK，不能直接当作当前 APK 的完整长期验收。

## 明确不支持或会拒绝的情况

| 情况 | 当前行为 | 要改变现状需要什么 |
| --- | --- | --- |
| 非调试 AOT **活动机器码帧**在暂停点立即转换成解释器帧 | 对无法确认可安全去优化的帧返回 66；旧定义保持完整，帧退出后可重试。两种 Hook 顺序的拒绝、旧帧返回和重试已验证：[结果一](evidence/source-engine/20261006T091127Z/probe.json)、[结果二](evidence/source-engine/20261006T091149Z/probe.json)。 | 证明目标编译策略的栈图可恢复暂停点的 Dex 寄存器，再验证实际切换、内联及并发帧；不能只放开预检。详见 [TODO.md](TODO.md)。 |
| 添加方法 `final`，但已有加载的子类覆写它 | 返回 71，事务不提交。这是已验证的冲突拒绝，不应强行提交。[专项](evidence/source-engine/20261006T093015Z/probe.json) | 若需支持，必须定义并实现现存子类及旧调用点的一致语义。 |
| 添加类 `final`，但已有加载的子类 | 返回 70，事务不提交。这是类关系冲突。[能力说明](README.md#能力与版本选择) | 同上，不能仅改修饰符位。 |
| 类或方法变为 abstract 后，存活的直属实例或已加载子类将继承未实现的抽象方法 | 事务返回 71，旧定义不变。[能力说明](README.md#能力与版本选择) | 需要先消除依赖，或设计安全的对象迁移和分派语义。 |
| 非当前 Android 13 / arm64 / 固定 ART Build-ID 的系统 | 平台入口或私有 Hook 校验拒绝；尚无其他版本后端。[版本选择](README.md#能力与版本选择) | 对每个新版本建立独立后端、源码 ABI 校准和设备回归。 |
| 未打包或未启用源码演进模块时的 `EVOLVE_SCHEMA` | 明确返回不可用，不自动退化成较弱的重定义。[公共 API](README.md#能力与版本选择) | 构建时加入 `-PsourceEngine13`，运行时调用 `enableEvolution()`。 |

## 已有部分实现，但仍缺关键验证

| 能力或风险 | 已有证据与缺口 | 完成判据 |
| --- | --- | --- |
| boot JIT 帧内联应用方法，且暂停时尚未执行到内联点 | 预检已有保守扫描；`ArrayList.forEach` → 应用回调实验没有产生目标内联帧，目标 `HGraph::InlineInto` 命中 0 次。[诊断](evidence/source-engine/20261006T084146Z/probe.json) | 构造真实目标帧，在两种 Hook 顺序下证明继续执行失效指令前已切换，或旧定义未改动时安全拒绝。按用户要求暂缓，详见 [TODO.md](TODO.md)。 |
| 更广的 AOT/JIT 编译调用形态 | 已验证部分应用 AOT 活动帧、JIT 内联帧和两个并发内联帧；优化内联、更多并发帧、不同调用形式混合以及长期编译策略未穷尽。非调试 AOT 即时 DeOPT 仍不支持。[编译执行记录](README.md) | 为每类目标形态取得实际机器码栈、提交结果及恢复后返回值，并覆盖双 Hook 顺序。 |
| 接口移除和旧父类移除的全部语义 | 已测接口默认方法、缓存特殊句柄、三代关系、部分 AOT 与批次；其他默认方法、缓存接口引用、较大批次和优化调用形态未穷尽。总括标志 `hierarchyChange=false`。[当前状态](evidence/status.json) | 扩大调用与缓存矩阵，验证新旧类型关系、异常、对象状态和无效批次原子性。 |
| 成员修饰符的全部组合与旧权限缓存 | 单项转换和若干组合已测；其他组合、旧解析访问权限收窄的全部调用形式尚未验证。总括标志 `memberModifierChange=false`。[当前状态](evidence/status.json) | 对合法组合及旧字节码、反射、JNI、MethodHandle、VarHandle 和 JIT 调用逐项验证。 |
| 存活直属实例参与类 `abstract` 切换时的优化执行 | 当前使用持续到进程结束的进程级解释执行回退；语义专项已测，但尚未恢复优化执行。[能力说明](README.md#能力与版本选择) | 在旧实例、子类、GC 和编译调用同时存在时实现并验证安全的有限范围回退或恢复策略。 |
| 元数据完整回收与长期 ClassLoader 生命周期 | 两种 Hook 顺序各通过 256 轮混合字段/方法退役、后台 GC 和类加载器卸载；其他缓存引用形式及更长时间仍未覆盖。总括标志 `metadataReclamation=false`。[证据一](evidence/source-engine/20261006T044759Z/probe.json)、[证据二](evidence/source-engine/20261006T044857Z/probe.json) | 扩展旧反射、JNI 与句柄保活/释放矩阵，做长期内存与卸载验证。 |
| 长期运行及前台交互下的稳定性 | 旧 APK 有前台绘制和 1000 次提交证据，也有过 ART 恢复路径崩溃；当前 APK 的完整 class redefine 回归已通过，但尚未完成对应时长的实体屏幕验收。[历史记录](README.md) | 用当前 APK 做长时结构提交、实体屏幕绘制、焦点变化和多轮独立启动验证，记录崩溃与资源趋势。 |
| 失效接收者的绑定 `MethodHandle` 异常语义 | ART 在已测形态会抛 `IllegalStateException`；是否应统一为其他链接异常尚需审查。[现有说明](README.md) | 对照目标语义，覆盖新旧句柄和 JIT 调用后决定是否修改。 |

## 使用这些记录的原则

- `capabilities()` 中单项为 `true` 只证明该项已测范围；总括的 `memberModifierChange`、`hierarchyChange`、`metadataReclamation` 和 `fullDcevmComplete` 当前均为 `false`。[状态文件](evidence/status.json)
- 返回 66、70、71 等拒绝结果时，应把本次替换视为**未提交**，不要假定新定义部分生效；依照具体冲突消除原因或等待活动帧退出再重试。
- 后续完成一项时，应同时更新这里的现状、目标设备证据和总括能力标志；旧 APK 的专项通过不能自动证明新 APK 的长期稳定性。
