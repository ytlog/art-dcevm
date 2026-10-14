# 能力限制

本文汇总使用时需要知道的前提、会被拒绝的变化以及各类变化的语义。实现机制见 [STRUCTURAL_REDEFINE_ENGINE.md](STRUCTURAL_REDEFINE_ENGINE.md)。

## 前提限制

- 应用必须从进程启动时就是 Java 可调试的；release 进程中 ART 不开放 `can_redefine_classes`，本库无法工作。
- 仅支持 arm64-v8a 的 Android 11–17。
- 运行库缺少基础入口的任一命名符号，或公开 JVMTI 表与找到的命名包装器不一致时初始化失败。方法与字段退役另需各自的名称；缺少时只关闭相应能力，并在 `capabilities()` 的诊断中说明。
- 退役依赖运行时探测确认 ART 的类布局声明；探测不通过的事务不允许退役，`capabilities()` 的 `memberMapping` 给出原因。
- 增强范围是标准 `RedefineClasses`。标准入口的批次读取和事务执行已串行化；直接调用 ART 结构重定义扩展的并发事务不参与该保护。

## 不支持的变化（以 JVMTI 错误拒绝，旧定义保持不变）

| 变化 | 错误码 | 说明 |
| --- | --- | --- |
| 方法在 static 与实例之间变化，或 varargs、strict 标志变化 | `UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED`（71） | 可改为删除旧方法、新增不同签名的方法 |
| 类在接口、注解、枚举之间变化 | `UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED`（70） | |
| 移除接口，以及任何失去父类型的父类变化 | `UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED`（66） | 已校验代码依赖旧类型关系，见引擎文档[收窄不支持](STRUCTURAL_REDEFINE_ENGINE.md#收窄不支持) |

方法删除/改签名与字段删除/改类型/存储变化在退役不可用时分别以 `UNSUPPORTED_REDEFINITION_METHOD_DELETED`（67）与 `UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED`（64）拒绝，native 改 Java 以 71 拒绝；新增接口与父类变化在没有探测通过的类布局时以 66 拒绝；父类变化还需要校验重定向（`capabilities()` 的 `superclassWidening`）。

## 语义冲突（会被拒绝）

- 方法新增 final，而已加载的子类覆写了它。
- 类新增 final，而已有已加载的子类（70）。
- 类新增 abstract，而该类自身仍有实例（70）；子类的实例不受影响。
- 新增的接口无法经目标类的加载器解析、不是接口，或对目标不可访问（`JVMTI_ERROR_FAILS_VERIFICATION`，62）。
- 新父类无法解析、是接口、是 final、不可访问，或新定义与已加载子类覆写了它的 final 方法（62）；新父类扩展了该类本身（`JVMTI_ERROR_CIRCULAR_CLASS_DEFINITION`，61）；新父类不扩展原父类（66，收窄）；同一批次还重定义了该类的父类、子类或新父类链上的类（66）。
- 退役不可用时，新增或改变访问级别的方法可能顶替已加载子类的接口 default/miranda 拷贝。

## 父类变化的语义

- 只支持放宽：新父类必须扩展原父类。已有实例保留自身与原父类链的字段值，新父类的字段为零值，不执行新父类的构造器；新父类在重定义前加载但不初始化。
- 虚方法按新层级分派，新父类覆写的方法对已有实例立即生效；已在执行的旧方法帧中的 `super` 调用仍按旧层级。
- ART 13 起校验新类时按名称解析到旧类，引擎在校验期间改为返回新类。若某版本的校验器走其他解析路径，重定义以 62 拒绝，旧定义保持不变。

## 类修饰符的语义

- 可见性、final、abstract、synthetic 与嵌套类的 InnerClass 标志可以改变，新类使用新修饰符。已解析的调用点与类型引用保留，ART 不对它们重新做访问检查；新的解析按新修饰符检查。

## 新增接口的语义

- 已有实例与已加载子类的实例随即成为新接口的实例。新增的接口在重定义前经目标类的加载器加载但不初始化，其静态初始化照常在首次需要时执行；该加载器随之成为这个接口的发起加载器。

## 方法种类的语义

- 删除的 native 方法与其他方法一样退役；正在执行它的线程继续执行。native 改为 Java 后，已有的调用点、缓存的反射对象与 JNI 方法 ID 执行新的 Java 方法体。
- 方法变为 abstract 后，没有实现它的子类实例被调用时抛 `AbstractMethodError`。
- 删除的构造器：字节码 `new`、JNI `NewObject*` 抛 `NoSuchMethodError`，缓存的 `Constructor.newInstance` 抛包装它的 `InvocationTargetException`。
- 删除类初始化器不影响已初始化的类，静态字段保留原值。

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
- 每次经标准入口重定义前，库通过目标类的定义加载器加载（不初始化）新定义引用的全部类，防止 ART 在事务中途才加载子类、使其留在旧类之下（见 [结构重定义引擎](STRUCTURAL_REDEFINE_ENGINE.md#事务中的类加载)）。事务期间库启用 JVMTI `ClassPrepare` 事件。若仍有目标类的子类在事务中加载（例如 ClassFileLoadHook 改写后才引用它，或其他线程并发加载），该次尝试会在提交前失败并自动重试，ClassFileLoadHook 随之再次被调用；连续三次失败时返回 `JVMTI_ERROR_INTERNAL`。

## 语义说明

- 已有对象的新增字段取零值或 `null`，不重新执行构造器；新建对象执行新构造器。
- 结构变化会让 ART 遍历堆迁移实例、去优化所有线程帧并清空 JIT 代码，耗时随堆大小增加，提交后需要重新预热。
- 补丁分发、签名校验和访问控制由调用方负责；`RetransformClasses` 不属于增强范围。
