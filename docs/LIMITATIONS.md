# 能力限制

Phase 1–2 已在一台 Android 16 / API 36、arm64 设备上通过指定的 12 次提交和行为检查：方法体、新增成员、成员修饰符变化、拒绝后的旧定义及批次原子性。其他设备和版本仍待回归，完整 DCEVM 尚未完成。设备与 APK 哈希见 [VERIFICATION.md](VERIFICATION.md)，各阶段计划见 [STRUCTURAL_REDEFINE_ENGINE.md](STRUCTURAL_REDEFINE_ENGINE.md)。

## 前提限制

- 应用必须从进程启动时就是 Java 可调试的；release 进程中 ART 不开放 `can_redefine_classes`，本库无法工作。
- 仅支持 arm64-v8a 的 Android 11–17，不含 12L。
- 运行库缺少 MVP 所需任一命名符号，或公开 JVMTI 表与找到的命名包装器不一致时初始化失败。包装器通过官方 ShadowHook 按名称读取 `.gnu_debugdata`，其他符号只查 `.dynsym`。
- 增强范围是标准 `RedefineClasses`。标准入口的批次读取和事务执行已串行化；直接调用 ART 结构重定义扩展的并发事务不参与该保护。

## 尚未支持的变化（以 JVMTI 错误拒绝，旧定义保持不变）

| 变化 | 错误码 | 计划 |
| --- | --- | --- |
| 删除字段，字段类型或 static 属性变化 | `UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED`（64） | Phase 3 |
| 删除方法，方法签名变化 | `UNSUPPORTED_REDEFINITION_METHOD_DELETED`（67） | Phase 3 |
| native → Java，abstract 相关的方法变化 | `UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED`（71） | Phase 3/4 |
| 类修饰符变化 | `UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED`（70） | Phase 4 |
| 父类、接口变化 | `UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED`（66） | Phase 4 |

## 语义冲突（各阶段都会拒绝）

- 方法新增 final，而已加载的子类覆写了它。
- 类新增 final，而已有已加载的子类。
- 变为 abstract 后，存活实例或已加载子类将继承未实现的抽象方法。

## ART 自身的限制

结构重定义沿用 ART 的检查：接口、数组、基本类型、代理类、`java.lang.String`、mirror 类、`java.lang.Thread` 及其子类不可重定义；在本线程类初始化过程中不能重定义该类；存在指针形式 JNI ID 的类不能做结构变化。

## 语义说明

- 已有对象的新增字段取零值或 `null`，不重新执行构造器；新建对象执行新构造器。
- 结构变化会让 ART 遍历堆迁移实例、去优化所有线程帧并清空 JIT 代码，耗时随堆大小增加，提交后需要重新预热。
- 补丁分发、签名校验和访问控制由调用方负责；`RetransformClasses` 不属于增强范围。
