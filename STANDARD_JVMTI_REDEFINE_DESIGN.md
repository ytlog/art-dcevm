# 通过标准 JVMTI `RedefineClasses` 提供 ART 类演进能力

状态：设计方案，尚未实现。目标范围为当前已适配的 Android 13 / API 33 / arm64、从进程启动时即可调试的 App。本文描述对外使用方式与实现路径；不扩大现有类演进能力和设备验证范围。

## 目标

增强库只扩展 ART 对标准 JVMTI `RedefineClasses` 的处理能力。用户编写的 Agent 仍传入 `jvmtiClassDefinition[]` 并调用 `jvmtiEnv::RedefineClasses`，不需要调用项目专有的 `EVOLVE_SCHEMA`、`PatchRequest` 或 Native 替换函数。

对外最多保留一次初始化。初始化成功后，标准方法体替换、已支持的增量结构变化和已支持的结构演进，都从同一个 JVMTI 入口按整批补丁路由。未支持或无法安全执行的变化以 JVMTI 错误拒绝，保持旧类定义完整。

本方案只接管 `RedefineClasses`；`RetransformClasses` 的语义与覆盖范围需要另行设计，不随本次入口改造自动扩大。

## 预期使用方式

建议先采用显式初始化，确保增强库先于用户 Agent 的第一次重定义调用就绪：

```java
// App 启动时执行一次；接口名仅示意。
DcevmRuntime.initialize(context);
```

用户 Agent 保持标准 JVMTI 调用：

```cpp
jvmtiClassDefinition definitions[] = { /* 目标 Class 与新 DEX */ };
jvmtiError result = jvmti->RedefineClasses(count, definitions);
```

Java 初始化函数负责附加增强库自身的 JVMTI Agent 入口，加载内部源码模块，并安装进程级路由与守卫。用户 Agent 可以在此后单独附加；其业务代码负责取得或生成补丁、决定替换时机和处理错误。增强库负责虚拟机层面的校验、迁移、代码失效与一致性。

`System.loadLibrary()` 只能加载普通 Native 库，不能单独保证 JVMTI 插件和 Agent 已附加。当前 Android 路径应由初始化函数封装 `Debug.attachJvmtiAgent()`。仅把 SO 放入 APK 不会自动修改运行中的 ART；将来可由 App 启动组件自动调用同一初始化函数，但仍存在实际的加载与附加步骤，并需保证它先于用户 Agent 的首次重定义。

## 模块职责

| 模块 | 职责 |
| --- | --- |
| 增强库的 Agent 入口 | 取得 JVMTI 环境、检查设备和能力、初始化内部模块、安装标准入口 Hook；不承载用户业务逻辑。 |
| 核心后端 | 串行化全部重定义请求，分析整批 DEX 与当前存活类，选择事务路径，统一返回 JVMTI 错误。 |
| 源码演进模块 | 在设备 ART 事务中实现已支持的结构变化、活动帧保护和旧引用语义。 |
| 用户 Agent | 使用自己的 JVMTI 能力执行业务逻辑；提交类定义时只调用标准 `RedefineClasses`。 |

这些库与手机系统的 `libart.so`、`libopenjdkjvmti.so` 位于同一个 App 进程。增强库不替换系统 ART，也不要求手机运行时持有 AOSP 源码目录。

## 标准入口的接管位置

目标 Android 13 源码中的调用链为：

```text
用户 Agent 的 jvmtiEnv::RedefineClasses
  → libopenjdkjvmti 的 JVMTI 函数表入口
  → openjdkjvmti::Redefiner::RedefineClasses
  → 原 ART 普通重定义路径
```

优先在 `openjdkjvmti::Redefiner::RedefineClasses` 的**命名 ELF 符号**上安装 Inline Hook。该函数由库内部直接调用，单纯 PLT Hook 不保证覆盖。当前工程已有按动态符号表或普通符号表查找、再安装 Inline Hook 的机制；继续遵守固定 Build ID 与源码 ABI 校准要求，不依据反汇编计算地址或对象偏移。

Hook 只作为路由入口；原函数指针必须保存。进入 Hook 时保留调用者原始的 `jvmtiEnv*`、类数组、DEX 和返回码语义。不得把外部 Agent 的请求改用增强库自己的 JVMTI 环境执行，以免改变能力检查及事件归属。若目标符号、Build ID、源码模块或校准失败，初始化应明确失败，不能宣称已完成增强。

## 整批请求的路由

在修改任何类之前，对**整个批次**读取当前存活类结构并检查新 DEX。不要先尝试普通重定义、收到错误后再重试结构路径；首次尝试可能已经运行转换回调，不能作为无副作用的分类手段。

| 整批所需能力 | 路由 |
| --- | --- |
| 仅包含标准允许的方法体更新 | 调用保存的 ART 原 `RedefineClasses`。 |
| 包含 ART 原生扩展已经支持的增量结构变化 | 使用 ART 结构事务。 |
| 包含已实现的成员删除、签名或类型变化、父类或接口演进等 | 使用源码增强结构事务。 |
| DEX 无效、变化不受支持、活动编译帧无法安全处理 | 返回对应 JVMTI 错误；整批不提交。 |

同一批中只要有一个类需要更强的事务，就把全批交给该事务，不能逐类拆分后部分提交。提交路径还必须重新校验当前类状态；提交前的分析只是路由依据，不代替 ART 最终校验。

源码事务继续复用设备 ART 的分配、堆管理和最终线程暂停过程。对于会使旧类型关系失效的变化，先检查活动 AOT/JIT 栈帧；可以确认安全转换的帧在提交前完成 DeOPT，无法确认的帧拒绝本次事务，待其退出后由调用方重试。旧代码入口、JIT 缓存、解释器与解析缓存，以及旧反射/JNI/MethodHandle/VarHandle 引用仍按现有增强路径处理。

## 并发、事件和失败语义

- 标准入口 Hook 需要与现有 Java 测试入口共用一把**进程级事务锁**。用户 Agent 可能在不同线程、不同 JVMTI 环境中调用，不能只依赖当前 `NativeBridge.apply()` 的锁。
- 防止内部调用再次进入自身 Hook；源码事务调用 ART 原结构入口时，要使用明确的原函数入口和事务作用域。
- 保留 ART 对调用者 JVMTI 能力、类是否可修改、非法参数及错误码的检查。增强不能让没有 `can_redefine_classes` 能力的 Agent 获得替换权限。
- 核对 `ClassFileLoadHook` 等转换回调在各路由中的次数、顺序和所见 DEX。当前普通与结构路径的回调行为不同，不能只根据原始输入 DEX 分类后就假定最终结构不变。
- 初始化需要幂等。部分 Hook 安装失败时回滚已安装的 Hook；不能安全回滚时明确报告进程需重启。
- 错误码表示整批未提交。尤其是活动帧安全拒绝时，旧类及旧调用应继续正常工作，调用者可在活动帧退出后重试。

## 与当前实现的差距

当前 `JvmtiBodyRedefiner` 已调用标准 `RedefineClasses`，但结构演进只在专有 `apply(EVOLVE_SCHEMA)` 中设置 `SourceTransactionScope` 并调用源码模块。现有 Java API 还显式暴露 `analyze()`、`apply()`、`enableEvolution()`。因此目前尚不能宣称“任意用户 Agent 调用标准 `RedefineClasses` 即得到增强能力”。

改造顺序建议为：

1. 把模块初始化整合成一次 Java 初始化，并验证目标设备 Build ID、源码 ABI、Hook 安装与失败回滚。
2. 在标准 JVMTI 重定义入口按名称安装 Inline Hook，加入重入保护和进程级事务锁。
3. 将现有整批分析用于路由，让结构事务接收调用者的 `jvmtiEnv*`；普通请求保留 ART 原路径。
4. 验证转换事件、整批原子性、权限和错误码，再将专有替换 API 从对外契约移出；其实现可暂留作内部测试入口。
5. 使用一个不链接项目替换 API 的独立用户 Agent 做设备验收。

## 验收条件

- 增强库初始化后，独立用户 Agent 仅调用标准 `RedefineClasses`，即可完成当前设备已支持的方法体、增量和结构演进样例。
- 未安装增强时仍保持手机原有 JVMTI 行为；增强初始化失败不报告成功。
- 两种 Agent 加载顺序、多个线程和多环境请求均保持能力检查及批次原子性。
- 活动非调试 AOT 帧等无法安全转换的情况仍拒绝提交；旧调用正确完成，帧退出后重试成功。
- 旧引用、JIT/AOT 调用、转换回调和错误码通过对应设备回归；`fullDcevmComplete` 仍只在完整语义验收后才能置为 `true`。

## 参考实现位置

- 当前 App 初始化：`app/src/main/java/dev/artdcevm/platform/android13/Android13Backend.java`
- 当前 Agent 与事务锁：`app/src/main/cpp/core/agent.cc`
- 当前后端分流：`app/src/main/cpp/platform/android13/android13_backend.cc`
- 当前源码事务入口：`app/src/main/cpp/platform/android13/source_engine_loader.cc`
- 当前命名符号 Hook：`app/src/main/cpp/hooks/shadow_hook_engine.cc`
- ART 标准入口源码：`build/art13-source/art/openjdkjvmti/OpenjdkJvmTi.cc`、`build/art13-source/art/openjdkjvmti/ti_redefine.cc`

外部依据：[Android ART TI](https://source.android.com/docs/core/runtime/art-ti)、[JVMTI `RedefineClasses` 规范](https://docs.oracle.com/en/java/javase/19/docs/specs/jvmti.html)。
