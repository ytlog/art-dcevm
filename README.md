# ART DCEVM

在非 root 的 Android 设备上，于应用进程内增强 ART 的类重定义能力，目标是 VM 级 DCEVM：运行期间增删字段和方法、修改类型与签名、修饰符和继承关系，存活对象保留兼容字段的值。适用于自有应用的开发调试和热重载工具。

**状态：Phase 1–3、Phase 4a、Phase 4b（新增接口）与 Phase 4c（放宽的父类变化）已在一台 Android 16 设备通过验证。** 标准 JVMTI 入口可执行方法体替换、新增字段和方法、成员与类修饰符变化、删除方法和改签名、删除字段、改字段类型与 static/实例互换、新增接口与放宽的父类变化，见 [验证记录](docs/VERIFICATION.md)。移除接口与收窄父类不支持（已校验代码依赖旧类型关系），其他 Android 版本尚待设备回归。退役成员的语义与限制见 [能力限制](docs/LIMITATIONS.md)。旧实现保存在本地标签 `backup-call-graph-engine-20261012`。

## 原理

Android 11 起，ART 的 `Redefiner::StructurallyRedefineClasses` 已经实现新类分配、堆遍历迁移实例、改写反射与 JNI 引用、全线程去优化和 JIT 失效，但只允许"只增不删"。本库用 ShadowHook 挂接标准 JVMTI 表入口与校验函数，路由到 ART 的结构事务；接管成员映射以退役删除或改签名的方法与字段，并在缓存的反射、JNI、MethodHandle 与 VarHandle 入口拒绝访问退役成员。提交、迁移与失效仍由 ART 执行；类分配策略的扩展属于 Phase 4。私有能力只通过命名 ELF 符号访问，不从反汇编推算地址或偏移。

## 要求

- Android 11–17（不含 12L），arm64-v8a。
- 应用从进程启动时就是可调试的（`debuggable`）。ART 只在这种进程里开放类重定义能力。
- 不需要 root。

## 使用方式

1. 初始化一次增强库：

   ```java
   DcevmRuntime.initialize(context);  // 失败时抛异常，附诊断信息
   ```

2. 用自己的 JVMTI Agent 申请 `can_redefine_classes`，并通过标准接口提交整批类定义（ART 接收 DEX 字节）：

   ```cpp
   jvmtiError error = jvmti->RedefineClasses(count, definitions);
   ```

   尚未支持的变化以 JVMTI 错误拒绝，整批不提交，旧定义保持可用。`DcevmRuntime.capabilities()` 返回当前已支持的变化类别。

仓库中的 [standard_jvmti_client.cc](app/src/debug/cpp/testing/standard_jvmti_client.cc) 是测试用的独立 Agent 示例，[fixtures](fixtures) 是测试补丁源码。

## 构建

需要 JDK 17+、Android SDK 37、NDK 27.2.12479018、CMake 4.3.4，并在 `local.properties` 中配置 `sdk.dir`。

```sh
./gradlew :app:assembleDebug :artdcevm:assembleRelease
```

ShadowHook 以官方 Maven 依赖（`com.bytedance.android:shadowhook`）引入。发布方式见 [MAVEN_PUBLISHING.md](docs/MAVEN_PUBLISHING.md)。

## 文档

- [引擎方案](docs/STRUCTURAL_REDEFINE_ENGINE.md)
- [能力限制](docs/LIMITATIONS.md)
- [Android 兼容性](docs/ANDROID_COMPATIBILITY.md)
- [验证记录](docs/VERIFICATION.md)
- [真机 ART 运行库采集](docs/ART_DEVICE_COLLECTION.md)
