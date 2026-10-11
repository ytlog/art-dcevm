# 任务书：按结构重定义方案重写引擎（Phase 1–2）并清除旧代码

执行者先读 [AGENTS.md](../AGENTS.md) 和 [STRUCTURAL_REDEFINE_ENGINE.md](STRUCTURAL_REDEFINE_ENGINE.md)。本任务**完全按新方案写代码，不兼容旧代码**：删除与方案无关的实现、测试和工具，不保留转发、别名或开关。旧代码在本地分支 `backup/call-graph-engine-20261012`（标签 `backup-call-graph-engine-20261012`）中保存，需要参考时从那里读取。本任务完成后删除本文件。

## 1. 已核对的事实（不必重新调研，但实现时引用源码处须与之一致）

- **符号**：`platform/symbols/structural_redefine_symbols.h` 中 17 个符号在 27 套设备库（API 30–37）的 `.dynsym` 中全部存在，名称跨版本一致；`python3 tools/check-structural-symbols.py` 复核。`Redefiner::RedefineClasses` 只有 4–8 字节（尾跳转），旧 Android 13 引擎曾用 ShadowHook 在同样大小的函数上安装成功。
- **调试前提**：AOSP `openjdkjvmti/art_jvmti.h` 中 `can_redefine_classes` 属于 `kNonDebuggableUnsupportedCapabilities`；`IsFullJvmtiAvailable()` 只认 `IsJavaDebuggableAtInit()` 或强制纯解释。用自有 jvmtiEnv 的 `GetPotentialCapabilities().can_redefine_classes` 作为前提证明。
- **入口**：`Redefiner::RedefineClasses` 与 `StructurallyRedefineClasses` 都是静态成员，签名 `jvmtiError(jvmtiEnv*, jint, const jvmtiClassDefinition*)`；后者先检查 `can_redefine_classes` 再执行同一个 `RedefineClassesGeneric<kStructural>`，`ClassFileLoadHook` 在其中照常调用。没有新增成员时 `IsStructuralRedefinition()` 为 false，ART 仍走原地更新，仅方法体变化的行为不变。
- **`ClassRedefinition` 前缀（11–17 一致）**：`Redefiner* driver_`@0、`jclass klass_`@8、`std::unique_ptr<const DexFile> dex_file_`@16、`std::string class_sig_`@24、`ArrayRef original_dex_file_`@48、`bool added_fields_`@64、`added_methods_`@65、`has_virtuals_`@66。现有 `platform/abi/arm64/android11|13|15/class_redefinition.h` 与 `common/class_redefinition_prefix.h` 已用 `static_assert` 固定这些偏移。`has_virtuals_` 在 Android 16 源码中只写不读；如 11–15 也无读取者，则不必写它。
- **`Redefiner` 前缀**：`result_`（`int32_t`）@8，见 `platform/abi/arm64/common/redefiner_prefix.h`。`Redefiner::RecordFailure` 只做两件事：格式化 `*error_msg_` 和写 `result_`。自有校验拒绝时**只写 `driver_->result_` 并返回 false**，不构造平台 `std::string`；原因写入引擎诊断和 logcat。
- **`DexFile` 前缀（11–17 一致）**：vptr@0、`begin_`@8、`size_` 或 `unused_size_`@16（15 起为占位）、`data_begin`@24、`data_size`@32。文件大小一律从 DEX 头 `file_size_`（`begin + 0x20`）读取。重定义输入只有一个 ClassDef，索引为 0。
- **调用时机**：`CheckClass`/`CheckFields`/`CheckMethods` 由 `CheckRedefinitionIsValid` 在 `Redefiner::Run()` 中调用，位于 `ScopedSuspendClassLoading` 和 `ScopedSuspendAll` 之前，线程处于 Runnable；可以在其中调用 JVMTI/JNI 读取旧类结构。
- **放宽后会崩溃的位置（Phase 3/4 处理，本任务必须拒绝）**：`CollectNewFieldAndMethodMappings` 找不到同名同签名方法时 `CHECK` 失败；`UpdateClassStructurally` 内联的 `CopyFields` 找不到同名同类型（含 static 属性）字段或父类链不一致时 `CHECK` 失败。因此本任务中删除字段或方法、改类型或签名、static 与实例互换、改父类或接口，都必须在校验阶段拒绝。
- **错误码**（`third_party/jvmti/jvmti.h`）：63 METHOD_ADDED、64 SCHEMA_CHANGED、66 HIERARCHY_CHANGED、67 METHOD_DELETED、70 CLASS_MODIFIERS_CHANGED、71 METHOD_MODIFIERS_CHANGED、79 UNMODIFIABLE_CLASS。代码中使用枚举名，不写数字。

## 2. 目标代码结构

保持现有目录分层，只保留下列内容（其余删除）：

```
artdcevm/src/main/cpp/
  CMakeLists.txt                     # 只编译下列源文件 + ShadowHook
  core/agent.cc                      # Agent_OnAttach 创建后端；导出 nativeInitialize / nativeCapabilities / nativeErrorName
  api/hook_engine.h
  hooks/shadow_hook_engine.{h,cc}    # 去掉不再需要的 declared_symbol_lookup 依赖时同步精简
  platform/backend_factory.{h,cc}    # 只创建 StructuralBackend
  platform/symbols/art_symbol_compat.h            # 只留 Library / NamedSymbol / FindSymbol（可改为 inline，删除 .cc）
  platform/symbols/structural_redefine_symbols.h  # 已提交
  platform/abi/arm64/common/class_redefinition.h  # 合并为一份 11–17 共用前缀：driver/klass/dex_file/added_* 及 RedefinerPrefix、DexFilePrefix，附源码出处与 static_assert
  platform/common/structural_redefine_engine.{h,cc}  # 前提检查、Hook 安装、路由、校验 Hook
  platform/common/structural_policy.{h,cc}           # 纯函数：旧 ClassSchema vs 新 ClassSchema → 决策；主机可测
  platform/common/structural_backend.{h,cc}          # 唯一后端：自有 jvmtiEnv、能力报告、初始化
  platform/common/schema_snapshot.{h,cc}             # 公开 JVMTI 读取旧类结构（沿用）
  evolution/class_schema.h, evolution/dex_schema.cc  # DEX 解析（删除不再使用的接口）
  support/json.h
```

Java 库只保留 `api/DcevmRuntime`：

- `initialize(Context)`：检查 `FLAG_DEBUGGABLE`、`System.loadLibrary`、`Debug.attachJvmtiAgent`、调用 `nativeInitialize()`；失败抛异常并附诊断。成功后标准 `RedefineClasses` 即具备 Phase 1–2 能力。
- `capabilities()`：返回 JSON，逐项报告 `standardRedefineRouted`、`memberAddition`、`memberModifierChange`，以及尚未实现的 `memberDeletion`、`memberTypeChange`、`hierarchyChange`（均为 false），外加 `fullDcevmComplete=false` 和诊断字符串。
- 删除 `internal/*`、`src/bootstrap/*` 源码集、debug `NativeBridge`、`attachPublicJvmti`、`initializeCapabilities` 及其 consumer ProGuard 规则和 Gradle 配置。

## 3. Phase 1：标准入口路由

- `Engine::Install(HookEngine&, jvmtiEnv* own)`：
  1. 检查前提（见第 1 节）。
  2. 解析 `kStructurallyRedefineClasses` 地址并以 release 语义发布到原子变量。
  3. 在 `kRedefineClasses` 上安装 inline Hook，替代函数直接转调该地址。原 `RedefineClasses` 不再被调用，trampoline 槽位静态保存。
  4. 同时安装第 4 节的三个校验 Hook。任一步失败则卸载已装 Hook 并返回 `JVMTI_ERROR_NOT_AVAILABLE`。
  5. 幂等，进程级单例，由后端在自有互斥锁下调用。
- 不 Hook `RedefineClassesDirect`、不注册 `ClassFileLoadHook`，不改变调用者 env 的能力检查。

## 4. Phase 2：自有校验策略（修饰符变化）

三个 Hook 的替代函数签名为 `bool(ClassRedefinition* self)`（隐式 this）。

1. **`CheckClass`**：读取 `self->klass` 和 `self->dex_file`，用 `ReadMappedDexDeclarations`（`MappedDexMetadata{begin, file_size, data_begin, data_size, 0}`）解析新类，用 `live_schema::ReadLiveSchema(own_env, jni, klass)` 读旧类，交给 `structural_policy::Evaluate`。把结果按 `ClassRedefinition*` 缓存在线程局部，供随后同一对象的 `CheckFields`/`CheckMethods` 使用（同一线程顺序调用，`CheckMethods` 结束时清除）。描述符不一致时调用原函数，交给 ART 报 NAMES_DONT_MATCH 等原有错误。
2. **`CheckFields`/`CheckMethods`**：根据决策拒绝（写 `driver_->result_`，返回 false）或接受。接受且存在新增成员或修饰符变化时，调用命名的 `RecordNewFieldAdded`/`RecordNewMethodAdded`，确保 ART 走结构路径（新 Class 采用新 DEX 的修饰符；原地路径不会更新 ArtField/ArtMethod 的访问标志）。
3. **`structural_policy`**（纯函数，无 ART 依赖）：
   - 成员身份：字段为 (name, descriptor, static)，方法为 (name, descriptor)。修饰符比较前用 `body_only::MethodFlags` 同样的规则把 DEX 标志规范化为 JVMTI 视图。该函数随 `body_only_policy.h` 一起迁入 `structural_policy`，旧头文件删除。
   - **接受**：新增字段和方法；已有成员的 public/protected/private/包访问变化，以及 final、volatile、synchronized、transient、Java→native 变化。
   - **拒绝并给出错误码**：
     - 删除字段，或字段类型、static 属性变化 → SCHEMA_CHANGED
     - 删除方法或签名变化 → METHOD_DELETED
     - native→Java、abstract 相关变化 → METHOD_MODIFIERS_CHANGED
     - 类修饰符变化 → CLASS_MODIFIERS_CHANGED
     - 父类或接口变化 → HIERARCHY_CHANGED
     - 方法新增 final，且已加载子类覆写 → METHOD_MODIFIERS_CHANGED

     判断已加载子类：用 JVMTI `GetLoadedClasses`，结合 JNI `IsAssignableFrom` 和 `GetMethodID` 比较声明类。
   - 删除、类型变化和层级变化在 Phase 3/4 由自有映射与提交支持，届时只需放宽这里的规则。
4. 主机测试：为 `structural_policy` 编写主机单元测试，接入现有 `tests/host` 的运行方式；如现有运行方式只服务旧代码，则以新的单一脚本替换。

## 5. 删除范围

- **Native**：删除 `platform/common` 中除第 2 节列出文件外的全部文件（调用图、选择性解释、启动门控 `redefinition_gate`、`production_*`、`capability_backend`、各类 calibration/receiver/oracle/frame 模块等）；删除 `platform/android13`、`platform/android17`、`platform/jni_access`、`platform/jvmti`、`platform/symbols/android13`、`evolution/checked_method_resolution.*`、`evolution/migration_plan.*`，以及不再被引用的 `platform/abi/**` 头和 `support/*`。以编译通过且无未引用文件为准。
- **测试应用**：
  - 删除 `app/src/debug/java/**/testing` 下全部旧套件、`app/src/debug/cpp/testing` 中除 `standard_jvmti_client.cc` 外的 probe、`app/src/androidTest` 旧测试，以及 `platform/`、`external/` 中只服务旧测试的类。
  - 新建一套 instrumentation 测试：调用 `DcevmRuntime.initialize`，附加 `standard_jvmti_client` 作为独立用户 Agent，经标准 `RedefineClasses` 提交 `fixtures/` 生成的 DEX。
  - 只保留新测试用到的 fixture 原始类，`fixtures/` 目录与 `tools/build-fixtures.py` 保留，未被新测试使用的 fixture 子目录在 Phase 3/4 再启用，不删除。
- **工具**：保留 `check-public-privacy.py`、`check-structural-symbols.py`、`check-native-page-alignment.py`、`build-fixtures.py`、`collect-art-device.py`、`store-art-runtime-libs.py`、`apk_verification.py`、`evidence_paths.py`，以及它们依赖的辅助模块。删除其余只服务旧路线的 run/audit/test/verify/report 脚本、`tools/android13/`、`tools/native_support/`（若无保留者依赖）。新增一个 `tools/run-device-tests.py`，用于安装 APK、运行新 instrumentation 测试、核对 APK 哈希，原始输出写入 `build/evidence/`。
- **其他目录**：`frameproof/`、`tests/device` 中只服务旧路线的内容一并删除；`tests/host` 只保留新主机测试。
- **文档**：删除只描述旧路线的设计文档（`docs/` 下除 `README.md`、`ANDROID_COMPATIBILITY.md`、`ART_DEVICE_COLLECTION.md`、`MAVEN_PUBLISHING.md`、`STRUCTURAL_REDEFINE_ENGINE.md`、`VERIFICATION.md`、`LIMITATIONS.md` 外的文件，以及 `docs/articles/` 中的旧实现说明）。`VERIFICATION.md` 归档为一段说明，指向备份标签，再记录本次新验证。更新 `README.md`、`docs/README.md`、`LIMITATIONS.md`、`ANDROID_COMPATIBILITY.md`、`platform/README.md`，使之只描述新引擎；`STRUCTURAL_REDEFINE_ENGINE.md` 的目录落点表改为本节的实际文件。

## 6. 验证与验收

1. `./gradlew :app:assembleDebug :app:assembleDebugAndroidTest :artdcevm:assembleRelease` 通过，`-Werror` 下无警告；`check-native-page-alignment.py` 通过；主机策略测试通过。
2. `python3 tools/check-structural-symbols.py` 通过。
3. Android 16 设备，以匿名设备标识记录：
   - 方法体替换的行为与原 ART 一致。
   - 新增字段和方法经标准入口成功，已有对象的新字段为零值。
   - 修饰符变化用例（`fixtures/modifier-*`、`private-*` 中的可接受项）成功，并验证新访问语义。
   - 删除、类型变化和层级变化用例被拒绝，返回第 4 节约定的错误码，旧定义完整可用。
   - 无效批次整体不提交。
4. 在 `VERIFICATION.md` 记录命令、结论、设备型号和 API、APK SHA-256；原始日志放在 `build/evidence/`。
5. `DcevmRuntime.initialize` 在 Phase 1–2 完成后即可返回成功，但 `capabilities()` 中未实现的能力必须为 false，`fullDcevmComplete` 为 false。

## 7. 提交方式

按逻辑分成多个英文提交，每个提交都可编译，例如：

1. Remove call-graph engine and legacy test infrastructure
2. Add structural redefinition engine with standard entry routing
3. Add structural validation policy for member modifier changes
4. Rebuild device tests on the standard JVMTI client
5. Rewrite documentation for the structural engine

不要推送到远端。
