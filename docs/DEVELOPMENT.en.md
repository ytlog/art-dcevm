# Developing and extending ART DCEVM

English | [中文](DEVELOPMENT.md)

This guide covers building the library from source and integrating local changes while developing or extending it. For Maven integration and runtime usage, see the [README](../README.md).

## Build from source

Building from source requires JDK 17 or later, Android SDK 37, NDK 27.2.12479018 and CMake 4.3.4, with `sdk.dir` set in your local `local.properties`:

```sh
git clone https://github.com/ytlog/art-dcevm.git
cd art-dcevm

# Build the sample APK and the AAR for integration
./gradlew :app:assembleDebug :artdcevm:assembleRelease
```

The outputs are `app/build/outputs/apk/debug/app-debug.apk` and `artdcevm/build/outputs/aar/artdcevm-release.aar`. Within the same Gradle project you can depend on the source module directly:

```groovy
// app module build.gradle; settings.gradle must include :artdcevm
dependencies {
    implementation project(':artdcevm')
}
```

## Extend and verify

Keep shared runtime logic in `platform/common`, version-specific declarations in `platform/abi/arm64/androidNN`, and named symbols in `platform/symbols`, under `artdcevm/src/main/cpp/`. Use ART’s existing structural redefinition transaction for instance migration, reference rewriting, deoptimization and cache invalidation; see the [engine design](STRUCTURAL_REDEFINE_ENGINE.en.md).

Follow the [testing guide](TESTING.en.md) to validate changes, and consult [compatibility](COMPATIBILITY.en.md) for device coverage and [limitations](LIMITATIONS.en.md) for supported semantics.
