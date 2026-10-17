# ART DCEVM

[English](README.md) | 中文

ART DCEVM 是 Android 运行时类重定义增强库，用于在应用运行期间替换 Java 方法实现，并修改字段、方法、修饰符和受支持的继承关系。适用于自有应用的开发调试、热重载工具与类演进实验，无需 root 或修改系统分区。

## 前提

- Android 11–17、arm64-v8a（库的 `minSdk` 为 30）；各项变化按实际事务检查验收，已有设备覆盖见[兼容性结论](docs/COMPATIBILITY.md)。
- 应用从进程启动时就启用 `debuggable`，不能事后开启调试状态。
- 先初始化本库，再由自己的 JVMTI Agent 申请 `can_redefine_classes` 并提交标准 `RedefineClasses`。补丁生成、分发与新类提供由调用方负责。

## 支持与不支持的变化

- 普通类的方法体替换，字段/方法/构造器的新增、退役与恢复，字段类型及方法签名变化。
- 普通类成员与类修饰符，以及方法 native/Java、abstract/具体实现的受支持转换。
- 普通类新增接口，以及将父类放宽为原父类的真子类。
- 保留已有对象的兼容字段值；新增字段为零值或 `null`，不重跑已有对象的构造器。
- 多类同批原子提交；不支持的变化拒绝整批，旧定义保留。退役成员的缓存引用继续遵循退役语义。

不支持失去原有父类型、方法 static/实例调用方式切换、虚方法改为 private 或类/接口/注解/枚举种类切换。`RetransformClasses` 不在增强范围，缓存的原始字段偏移不受保护。完整限制、拒绝码与运行语义见 [LIMITATIONS.md](docs/LIMITATIONS.md)。

接口目标在 Android 13–16 / API 33–36 上支持 default/static/private 方法体更新、新增 public abstract/default/static 与 private 实例方法，任一方法的删除或改签名，abstract 与 default 互换，以及静态字段变化；可与相关实现类、子类或无关类原子提交。每次事务仍会检查支持条件；即使只改方法体也会重建受影响实例并保留兼容字段值。详见[接口重定义](docs/LIMITATIONS.md#接口重定义)。

## 快速开始

示例应用位于 [app](app)，独立用户 Agent 示例位于 [standard_jvmti_client.cc](app/src/debug/cpp/testing/standard_jvmti_client.cc)。

### 1. 构建与接入

通过 Maven 依赖接入，**无需单独声明 ShadowHook**：

```groovy
// settings.gradle
dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}
```

```groovy
// 应用模块 build.gradle
dependencies {
    implementation 'io.github.ytlog:artdcevm:0.1.0-alpha.2'
}
```

发布元数据自动声明 ShadowHook 运行时依赖，Gradle 会一并下载和打包。

也可以从源码构建。需要 JDK 17 或以上、Android SDK 37、NDK 27.2.12479018 和 CMake 4.3.4，并在本机 `local.properties` 中配置 `sdk.dir`：

```sh
git clone https://github.com/ytlog/art-dcevm.git
cd art-dcevm

# 构建示例 APK 和可接入的 AAR
./gradlew :app:assembleDebug :artdcevm:assembleRelease
```

产物分别为 `app/build/outputs/apk/debug/app-debug.apk` 和 `artdcevm/build/outputs/aar/artdcevm-release.aar`。在同一 Gradle 工程中可直接依赖源码模块：

```groovy
// 应用模块 build.gradle；settings.gradle 需已包含 :artdcevm
dependencies {
    implementation project(':artdcevm')
}
```

库的 `minSdk` 为 30，打包 arm64-v8a 的 native 库。应用需从启动时就启用 `debuggable`；不能在进程启动后再开启调试状态。

### 2. 初始化

在自己的 Agent 第一次重定义类之前，调用一次：

```java
import com.github.ytlog.artdcevm.api.DcevmRuntime;

// context 为应用 Context；附加或安装失败时抛出异常并附带诊断信息。
DcevmRuntime.initialize(context);
```

该方法加载 `libartdcevm.so`、附加增强 Agent 并接管标准 JVMTI 入口。同一进程中初始化成功后重复调用直接返回。

`DcevmRuntime.capabilities()` 返回诊断快照；提交与错误处理遵循下文的统一契约。

### 3. 附加自己的 JVMTI Agent

ART DCEVM 提供初始化入口；补丁加载与替换请求由调用方的 Agent 实现。自己的 Agent 需要取得 JVMTI 环境并申请重定义能力，例如：

```cpp
#include <jni.h>
#include "jvmti.h"

static jvmtiEnv* jvmti = nullptr;

extern "C" JNIEXPORT jint JNICALL
Agent_OnAttach(JavaVM* vm, char*, void*) {
    if (vm->GetEnv(reinterpret_cast<void**>(&jvmti), JVMTI_VERSION_1_2) != JNI_OK)
        return JNI_ERR;
    jvmtiCapabilities capabilities{};
    capabilities.can_redefine_classes = 1;
    return jvmti->AddCapabilities(&capabilities) == JVMTI_ERROR_NONE
        ? JNI_OK : JNI_ERR;
}
```

将自己的 Agent 编译、打包为 native 库后，可在应用中附加：

```java
import android.os.Debug;

Debug.attachJvmtiAgent("libmy_agent.so", null, context.getClassLoader());
```

`libmy_agent.so` 为调用方自行实现的库名，不是本仓库提供的库。Agent 构建需提供 JVMTI 头文件；仓库示例使用 [third_party/jvmti](third_party/jvmti)。

### 4. 提交替换类

Android ART 接收 **DEX 字节**。为目标类生成具有相同类名的新定义，将完整 DEX 数据与当前已加载的 `jclass` 交给自己的 Agent：

```cpp
// target：当前已加载的目标类。
// dex / dex_size：包含目标类新定义的完整 DEX 数据及长度。
// 调用线程已附加到 VM；jvmti 来自自己的 Agent。
jvmtiClassDefinition definition{};
definition.klass = target;
definition.class_byte_count = dex_size;
definition.class_bytes = dex;

jvmtiError error = jvmti->RedefineClasses(1, &definition);
if (error != JVMTI_ERROR_NONE) {
    // 将错误码交给调用方，并读取 lastRejection（见下文）；整批未生效。
}
```

多个相互依赖的类应组成同一个 `jvmtiClassDefinition[]`，一次提交完整批次。调用期间需保持类引用和 DEX 缓冲区有效。新类与新 lambda 的合成类须由调用方先提供给加载器，具体顺序见[提供新类](docs/LIMITATIONS.md#提供新类)。

## 错误处理

初始化成功后，**直接提交标准 `RedefineClasses`**。返回 `0`（`JVMTI_ERROR_NONE`）表示成功；非零时读取返回错误码（可用 JVMTI `GetErrorName` 取得名称）和 `lastRejection`，不要将失败请求视为已生效：

```java
// 在调用方的 Agent 返回非零错误码后读取。
String reason = DcevmRuntime.capabilities().optString("lastRejection", "");
```

`lastRejection` 描述最近一次标准入口事务的失败原因，成功时清空。98 表示接口运行时不可用，可提示重启应用进程后重试；其他拒绝规则与错误码见 [LIMITATIONS.md](docs/LIMITATIONS.md)。

**`capabilities()` 只用于诊断，不作为提交前的准入判断。不要根据其中的布尔值或状态字符串拦截提交；以实际事务的错误码与 `lastRejection` 处理结果。**

机制见[引擎设计](docs/STRUCTURAL_REDEFINE_ENGINE.md)，公开验证范围见[兼容性结论](docs/COMPATIBILITY.md)，构建与测试命令见[测试指南](docs/TESTING.md)。
