# 能力限制

Phase 1–3、Phase 4a 与 Phase 4b 的新增接口已在一台 Android 16 / API 36、arm64 设备上通过设备套件：方法体、新增成员、成员与类修饰符变化、普通类 concrete Java 方法的删除与签名替换、字段删除、改类型与 static/实例互换、新增接口，以及拒绝后的旧定义与批次原子性。其他版本的声明已按 AOSP 源码补齐并由运行时探测启用，但尚无设备证据；完整 DCEVM 尚未完成。设备与 APK 哈希见 [VERIFICATION.md](VERIFICATION.md)，设计与阶段计划见 [STRUCTURAL_REDEFINE_ENGINE.md](STRUCTURAL_REDEFINE_ENGINE.md)。

## 前提限制

- 应用必须从进程启动时就是 Java 可调试的；release 进程中 ART 不开放 `can_redefine_classes`，本库无法工作。
- 仅支持 arm64-v8a 的 Android 11–17。
- 运行库缺少基础入口的任一命名符号，或公开 JVMTI 表与找到的命名包装器不一致时初始化失败。方法与字段退役另需各自的名称；缺少时只关闭相应能力，并在 `capabilities()` 的诊断中说明。
- 退役依赖运行时探测确认 ART 的类布局声明；探测不通过的事务按 Phase 2 规则拒绝退役，`capabilities()` 的 `memberMapping` 给出原因。
- 增强范围是标准 `RedefineClasses`。标准入口的批次读取和事务执行已串行化；直接调用 ART 结构重定义扩展的并发事务不参与该保护。

## 尚未支持的变化（以 JVMTI 错误拒绝，旧定义保持不变）

| 变化 | 错误码 | 计划 |
| --- | --- | --- |
| 删除或改签名的方法是 native、abstract、接口声明、构造器或类初始化器 | `UNSUPPORTED_REDEFINITION_METHOD_DELETED`（67） | 待定 |
| native → Java，abstract 相关的方法变化 | `UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED`（71） | Phase 4 |
| 类在接口、注解、枚举之间变化 | `UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED`（70） | 不支持 |
| 父类变化 | `UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED`（66） | Phase 4c |
| 移除接口 | 66 | Phase 4b 收窄检查 |

方法删除/改签名与字段删除/改类型/存储变化在退役不可用的运行时上分别以 67 与 `UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED`（64）拒绝；新增接口在没有探测通过的类布局时以 66 拒绝。

## 语义冲突（各阶段都会拒绝）

- 方法新增 final，而已加载的子类覆写了它。
- 类新增 final，而已有已加载的子类（70）。
- 类新增 abstract，而该类自身仍有实例（70）；子类的实例不受影响。
- 新增的接口无法经目标类的加载器解析、不是接口，或对目标不可访问（`JVMTI_ERROR_FAILS_VERIFICATION`，62）。
- 变为 abstract 后，存活实例或已加载子类将继承未实现的抽象方法。
- 退役不可用时，新增或改变访问级别的方法可能顶替已加载子类的接口 default/miranda 拷贝。

## 类修饰符的语义

- 可见性、final、abstract、synthetic 与嵌套类的 InnerClass 标志可以改变，新类使用新修饰符。已解析的调用点与类型引用保留，ART 不对它们重新做访问检查；新的解析按新修饰符检查。

## 新增接口的语义

- 已有实例与已加载子类的实例随即成为新接口的实例。新增的接口在重定义前经目标类的加载器加载但不初始化，其静态初始化照常在首次需要时执行；该加载器随之成为这个接口的发起加载器。

## 退役成员的语义

- 删除的方法和签名替换前的方法退役：字节码、JNI、MethodHandle 再调用时抛 `NoSuchMethodError`，缓存的反射 `Method.invoke` 抛包装它的 `InvocationTargetException`；已在执行的旧方法帧继续执行旧方法体。
- 退役字段：字节码、JNI、MethodHandle 访问抛 `NoSuchFieldError`；缓存的反射 `Field` 访问实例字段抛 `IllegalArgumentException`，访问静态字段抛 `NoClassDefFoundError`；VarHandle 抛 `UnsupportedOperationException`。缓存的反射 `Field` 的名称与声明类会显示为库内占位类的字段。
- 恢复同名成员会产生新成员，旧的缓存引用保持退役。
- 发生过退役的类及其 class loader 保留到进程结束。

## ART 自身的限制

- 结构重定义沿用 ART 的检查：接口、数组、基本类型、代理类、`java.lang.String`、mirror 类、`java.lang.Thread` 及其子类不可重定义；在本线程类初始化过程中不能重定义该类；存在指针形式 JNI ID 的类不能做结构变化。
- `sun.misc.Unsafe`、`Atomic*FieldUpdater` 等缓存的原始字段 offset 不经过 ART 的任何入口；任何改变实例布局的结构重定义（包括新增字段）都会使其失效。

## 进程内的副作用

- 方法退役启用时，库通过 JVMTI `SetJNIFunctionTable` 包装 JNI 调用与字段访问；ART 设置覆盖表后会关闭本进程的 Core Platform API 检查，该检查只约束平台模块的调用方。

## 语义说明

- 已有对象的新增字段取零值或 `null`，不重新执行构造器；新建对象执行新构造器。
- 结构变化会让 ART 遍历堆迁移实例、去优化所有线程帧并清空 JIT 代码，耗时随堆大小增加，提交后需要重新预热。
- 补丁分发、签名校验和访问控制由调用方负责；`RetransformClasses` 不属于增强范围。
