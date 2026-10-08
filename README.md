# ART DCEVM

ART DCEVM 是一个 Android 运行时类重定义增强库，用于在应用运行期间替换 Java 类的方法实现，并在已适配的 ART 构建上修改字段、方法和继承关系。适合自有应用的开发调试、热重载工具和类演进实验。

应用必须从进程启动时就处于可调试状态。无需 root，无需修改系统分区。初始化后，调用方通过自己的 JVMTI Agent 使用标准 `jvmtiEnv::RedefineClasses` 提交替换请求。

**当前为实验实现。** 完整结构演进仅在已适配的 Android 13 / arm64 ART 构建上启用；其他设备可使用公开 JVMTI 路径。尚未达到完整 DCEVM 语义或生产环境稳定性要求。

## 功能

- **方法体替换**：更新已加载类的方法实现。
- **成员增删**：在已适配设备上新增、删除和重新添加字段、方法及构造器。
- **成员结构变更**：修改字段类型、方法签名，切换成员的 static / 实例形式，以及方法的 native / Java 实现。
- **修饰符变更**：支持已验证的访问权限、final、synchronized、volatile、abstract 变更及部分组合。
- **继承关系演进**：在已验证范围内扩展、移除或替换父类关系，移除和恢复接口关系。
- **存活对象处理**：保留兼容字段的已有值；已有对象新增字段使用零值或 `null`，不重新执行构造器。
- **旧引用保护**：对已退役成员和失效接收者，在已覆盖的字节码、反射、JNI、MethodHandle 与 VarHandle 路径上拒绝访问。
- **批量重定义**：通过一次 `RedefineClasses` 提交多个类，支持已验证的转换回调、并发请求及失败批次回滚。

以上结构演进功能均受设备适配和具体语义边界限制。已验证范围与未支持场景见 [能力限制](docs/LIMITATIONS.md) 和 [验证记录](docs/VERIFICATION.md)。

## Android 兼容性

| 路径 | 环境 | 能力 |
| --- | --- | --- |
| 公开 JVMTI | Android 11 / API 30 及以上，当前构建支持 arm64-v8a、x86_64 | 方法体替换；ART 暴露结构扩展时可经扩展入口新增成员 |
| 增强结构演进 | 已匹配并校准的 Android 13 / API 33 / arm64 ART 构建 | 通过标准 `RedefineClasses` 使用上述结构演进功能 |

目前完整演进的真机验证目标为 Xiaomi CAS，系统指纹为 `Xiaomi/cas/cas:13/TKQ1.221114.001/V816.0.4.0.TJJCNXM:user/release-keys`。启用条件还包括实际运行库的 Build ID、必需命名符号和运行时行为校准；同型号、同 Android 版本不等于兼容，系统或 ART 更新后也可能无法启用。

Android 11–17 的公开路径验证和私有适配进展见 [Android 兼容性](docs/ANDROID_COMPATIBILITY.md)。x86_64 构建用于公开 JVMTI 验证，不提供完整结构演进。

## 快速开始

示例应用位于 [app](app)，独立用户 Agent 示例位于 [standard_jvmti_client.cc](app/src/debug/cpp/testing/standard_jvmti_client.cc)。

### 1. 构建与接入

发布到 Maven Central 后，在应用工程中配置仓库和依赖即可，**无需单独声明 ShadowHook**：

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
// 应用模块 build.gradle；x.y.z 替换为已发布版本
dependencies {
    implementation 'com.github.ytlog:artdcevm:x.y.z'
}
```

`com.github.ytlog:artdcevm` 是发布脚本默认坐标，当前尚未发布到 Maven Central。发布元数据自动声明 ShadowHook 运行时依赖，Gradle 会一并下载和打包。当前默认发布包包含 arm64-v8a；公开 JVMTI 的 x86_64 测试包使用独立坐标 `com.github.ytlog:artdcevm-x86_64`。发布者操作见 [Maven 发布指南](docs/MAVEN_PUBLISHING.md)。

发布前可通过源码模块接入。仓库使用 Gradle Wrapper 9.4.1、AGP 9.2.0、Android SDK 37、NDK 27.2.12479018 和 CMake 4.3.4；需准备兼容的 JDK（17 或以上），并在本机 `local.properties` 中配置 `sdk.dir`。

```sh
git clone https://github.com/ytlog/art-dcevm.git
cd art-dcevm

# 构建示例 APK 和可接入的 AAR，默认 arm64-v8a
./gradlew :app:assembleDebug :artdcevm:assembleRelease
```

产物分别为 `app/build/outputs/apk/debug/app-debug.apk` 和 `artdcevm/build/outputs/aar/artdcevm-release.aar`。构建 x86_64 版本时使用 `-PtargetAbi=x86_64`。

在同一 Gradle 工程中接入源码模块：

```groovy
// 应用模块 build.gradle；settings.gradle 需已包含 :artdcevm
dependencies {
    implementation project(':artdcevm')
}
```

库的当前 `minSdk` 为 30。应用需打包对应 ABI 的 native 库，并从启动时就启用 `debuggable`；不能在进程启动后再开启调试状态。

### 2. 初始化

在自己的 Agent 第一次重定义类之前，调用一次：

```java
import com.github.ytlog.artdcevm.api.DcevmRuntime;

// context 为应用 Context；附加或适配失败时抛出异常。
DcevmRuntime.initialize(context);
```

该方法加载 `libartdcevm.so`、附加增强 Agent、检查设备适配并安装增强路由。同一进程中初始化成功后重复调用直接返回。初始化失败时不会自动降级，应处理异常后停止结构演进请求。

如果只需要公开 JVMTI 能力，可显式选择：

```java
import org.json.JSONObject;

JSONObject capabilities = DcevmRuntime.attachPublicJvmti(context);
```

返回的 JSON 用于查询当前后端能力。公开附加成功不代表完整结构演进初始化成功；公开 ART 结构扩展也不意味着标准 `RedefineClasses` 可以新增成员。

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

## 使用注意

- 对象新增字段初值为零或 `null`；新建对象仍按新定义执行构造器。字段类型或 static / 实例形式变化按旧成员退役、新成员创建处理。
- 删除成员、收窄继承关系后，旧引用可能抛出链接或访问异常。业务代码需处理这些语义变化。
- 已加载子类覆写将变为 final 的方法、存活实例将继承未实现抽象方法等冲突，会导致事务拒绝。
- 活动编译帧的处理受已验证路径限制。活动非调试 AOT 帧不支持即时去优化，部分请求需等待旧帧退出后重试；某些类演进会触发进程级解释执行回退。
- `RetransformClasses` 不属于增强结构演进入口。补丁分发、签名校验、访问控制和发布系统需由调用方实现。

## 示例与验证

```sh
# 在已适配的 Android 13 arm64 设备运行完整演进回归
python3 tools/android13/run-source-probe.py --serial DEVICE

# 在其他设备运行公开 JVMTI 回归；APK 需匹配设备 ABI
python3 tools/run-public-jvmti.py --serial DEVICE
```

测试脚本安装并操作示例应用 `dev.artdcevm.lab`，会结束其旧进程并从初始类定义开始运行。原始报告保存到本地 `build/evidence/`，不随仓库发布。`run-source-probe.py` 是沿用的脚本名称，当前构建不需要下载或编译 AOSP，也不加载独立源码引擎。

## 文档

- [开发说明与语义细节](docs/README.md)
- [Android 兼容性与适配进展](docs/ANDROID_COMPATIBILITY.md)
- [未支持及未充分验证的能力](docs/LIMITATIONS.md)
- [测试命令与验证结论](docs/VERIFICATION.md)
- [标准 JVMTI 入口设计](docs/STANDARD_JVMTI_REDEFINE_DESIGN.md)

项目使用 ShadowHook 安装命名符号 Hook，部分声明依据 AOSP ART 源码；第三方 JVMTI 头文件的来源与许可见 [ORIGIN.txt](third_party/jvmti/ORIGIN.txt)。
