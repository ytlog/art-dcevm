# 开发与扩展 ART DCEVM

[English](DEVELOPMENT.en.md) | 中文

本文介绍从源码构建本库，以及开发扩展时接入本地修改的方法。Maven 接入与运行时用法见 [README](../README.zh-CN.md)。

## 从源码构建

从源码构建需要 JDK 17 或以上、Android SDK 37、NDK 27.2.12479018 和 CMake 4.3.4，并在本机 `local.properties` 中配置 `sdk.dir`：

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

## 扩展与验证

在 `artdcevm/src/main/cpp/` 下，通用运行时逻辑放在 `platform/common`，版本声明差异放在 `platform/abi/arm64/androidNN`，命名符号放在 `platform/symbols`。实例迁移、引用改写、去优化与缓存失效复用 ART 已有的结构重定义事务，机制见[引擎设计](STRUCTURAL_REDEFINE_ENGINE.md)。

按[测试指南](TESTING.md)验证改动，并参照[兼容性结论](COMPATIBILITY.md)确认设备覆盖范围、[限制说明](LIMITATIONS.md)确认受支持的语义。
