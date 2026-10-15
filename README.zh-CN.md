# ART DCEVM

[English](README.md) | 中文

ART DCEVM 是一个 Android 运行时类重定义增强库，用于在应用运行期间替换 Java 类的方法实现，并修改字段、方法、修饰符和继承关系。适合自有应用的开发调试、热重载工具和类演进实验。

应用必须从进程启动时就处于可调试状态。无需 root，无需修改系统分区。初始化后，调用方通过自己的 JVMTI Agent 使用标准 `jvmtiEnv::RedefineClasses` 提交替换请求。

## 功能

- **方法体替换**：更新已加载类的方法实现。
- **成员增删**：新增、删除和重新添加字段、方法及构造器，包括 native、abstract 方法与类初始化器。
- **成员结构变更**：修改字段类型、方法签名，切换字段的 static / 实例形式，以及方法的 native / Java、abstract / 具体实现。
- **修饰符变更**：修改成员和类的访问权限、final、abstract、synchronized、volatile 等修饰符；实例方法不能改为 private。
- **继承关系演进**：新增接口，将父类替换为原父类的子类。
- **存活对象处理**：保留兼容字段的已有值；已有对象新增字段使用零值或 `null`，不重新执行构造器。
- **旧引用保护**：已删除或被替换的成员在字节码、反射、JNI、MethodHandle 与 VarHandle 路径上拒绝访问。
- **批量重定义**：通过一次 `RedefineClasses` 提交多个类；不支持的变化以 JVMTI 错误拒绝，整批不生效，旧定义保持可用。

实例迁移、引用改写、去优化和 JIT 失效均由设备 ART 自带的结构重定义事务完成，本库只接管其中的校验、成员映射和类分配决策，原理见 [引擎方案](docs/STRUCTURAL_REDEFINE_ENGINE.md)。

## 接口方法体修改

Android 13（API 33）和 Android 16（API 36）的 arm64 设备支持顶层与嵌套接口的默认、静态、私有方法函数体修改。接口可与实现类、实现类子类或无关类同批，也可提交多个接口和父子接口；整批原子提交。其他 Android 版本自动停用并报告原因，运行时探测仍须通过。

接口不支持成员或父接口演进：新增方法 63、删除／改签名 67、字段变化 64、方法修饰符变化 71、父接口变化 66。无共同批次接口祖先却共享实现类的两个接口，以及接口重建组之外的实现类父类同批结构变化，以 66 拒绝；Proxy 实现类、boot 接口和 pointer-id 标记以 79 拒绝；运行时能力或探测不可用以 98 拒绝。

即使只修改接口方法体，也会重新分配所有实现类及其子类的实例；兼容字段值保留。接入前请读取 `DcevmRuntime.capabilities()`，完整范围与错误语义见[能力限制](docs/LIMITATIONS.md#接口方法体重定义)。

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
    implementation 'io.github.ytlog:artdcevm:0.1.0-alpha'
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

`DcevmRuntime.capabilities()` 返回 `JSONObject`，包含当前支持的变化类别、诊断信息和最近一次拒绝的原因。

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
    // 记录错误码并处理失败，不将此次请求视为已生效。
}
```

多个相互依赖的类应组成同一个 `jvmtiClassDefinition[]`，一次提交完整批次。调用期间需保持类引用和 DEX 缓冲区有效。示例应用的 [补丁源码](fixtures) 与 [DEX 构建脚本](tools/build-fixtures.py) 展示了测试补丁的生成方式。

### 5. 新增类

`RedefineClasses` 只替换已加载的类。补丁中 APK 没有的类（新类，以及新 lambda 等合成类）由工具经类加载器提供，本库不定义新类，这与 Java 上的 DCEVM 和热重载工具的分工相同。工具对一次更新按以下顺序处理：

1. 每个类各编成一个 DEX（D8 `--file-per-class` 会把新 lambda 的合成类单独输出）。
2. 用 `Class.forName(name, false, 应用类加载器)` 分类：找得到的是已有类，尚未使用过的已有类也会因此加载，之后才能重定义；找不到的是新类。
3. 把新类打成一个 jar 并设为只读，再用 ART 的 JVMTI 扩展 `com.android.art.classloader.add_to_dex_class_loader` 把它追加到应用类加载器（`BaseDexClassLoader`）的路径末尾。Android 14 起，目标 SDK 为 34 及以上的应用不能加载可写的 DEX 文件。
4. 用一次 `RedefineClasses` 提交全部已有类。新定义引用的追加类，包括被重定义类的新子类，由本库在事务开始前加载。

追加只能加在加载器路径末尾，无法撤销，也不会覆盖已加载的类；追加过的类再次修改时，按已有类重定义。新类的静态初始化器在首次使用时执行，被重定义类的静态初始化器不会重新执行。示例实现见 [HotReloadTool.java](app/src/debug/java/com/github/ytlog/artdcevm/testing/HotReloadTool.java)。

## 使用注意

- 对象新增字段初值为零或 `null`；新建对象仍按新定义执行构造器。字段类型或 static / 实例形式变化按旧成员退役、新成员创建处理。
- 删除或改签名的成员退役后，旧引用再访问会抛出 `NoSuchMethodError`、`NoSuchFieldError` 等异常；已在执行的旧方法帧继续执行旧方法体。业务代码需处理这些语义变化。
- 方法保持原有的调用方式：在 static 与实例之间切换、实例方法改为 private，或把删除的方法以另一种形式加回，都会被拒绝，因为已校验的调用方仍会按旧方式调用它。
- 移除接口或让类失去原有父类型的继承变化会被拒绝。已加载子类覆写将变为 final 的方法、自身仍有实例的类改为 abstract 等冲突，也会导致事务拒绝。
- 结构变化会遍历堆迁移实例、去优化所有线程帧并清空 JIT 代码，耗时随堆大小增加，提交后需要重新预热。
- `Unsafe`、`Atomic*FieldUpdater` 等缓存的字段偏移在实例布局变化后失效。
- `RetransformClasses` 不属于增强范围。补丁分发、签名校验、访问控制和发布系统需由调用方实现。

完整的语义说明见 [能力限制](docs/LIMITATIONS.md)。

## 示例与验证

```sh
# 构建示例 APK 后，在连接的 arm64 设备上运行完整设备回归
python3 tools/run-device-tests.py --serial DEVICE
```

测试脚本安装并操作示例应用 `dev.artdcevm.lab`，会结束其旧进程并从初始类定义开始运行。原始报告保存到本地 `build/evidence/`，不随仓库发布。

## 文档

- [引擎方案](docs/STRUCTURAL_REDEFINE_ENGINE.md)
- [能力限制与语义细节](docs/LIMITATIONS.md)
