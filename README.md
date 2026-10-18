# ART DCEVM

English | [中文](README.zh-CN.md)

ART DCEVM enhances class redefinition on Android. It replaces Java method implementations while an app runs and changes fields, methods, modifiers and supported inheritance relationships. It is intended for development/debugging of your own apps, hot-reload tools and class-evolution experiments, without root or system partition changes.

## Prerequisites

- Android 11–17, arm64-v8a (`minSdk` 30). Actual transaction checks validate each change; see [compatibility](docs/COMPATIBILITY.en.md) for existing device coverage.
- The app must enable `debuggable` from process startup; it cannot be enabled afterwards.
- Initialize this library first. Your own JVMTI agent requests `can_redefine_classes` and submits standard `RedefineClasses`. Patch generation/distribution and providing new classes belong to the caller.

## Supported and unsupported changes

- Ordinary-class body replacement; addition, retirement and restoration of fields/methods/constructors; field types and method signatures.
- Supported ordinary member/class modifier changes and native/Java or abstract/concrete transitions.
- Adding interfaces to ordinary classes and widening a superclass to a proper subclass of the original superclass.
- Preserving compatible field values in live objects; new fields start at zero or `null`, without rerunning existing objects’ constructors.
- Atomic multi-class batches: unsupported changes reject the batch and preserve old definitions. Cached references to retired members retain retirement semantics.

Dropping existing supertypes and changing class/interface/annotation/enum kind are unsupported; class static/instance changes make old calls throw `IncompatibleClassChangeError` and keep old caller classes interpreted. Making a virtual method private makes old external calls without access throw `IllegalAccessError`; where the runtime exports the target kind's named switch-interpreter selector and Class/Thread/HandleScope probes pass, old target frames call the new private body directly, ignoring overrides. `privateSelfSelectionClass` and `privateSelfSelectionInterface` report services separately; `privateSelfSelection` remains their conjunction. API 36 is device-verified; interface-only templates in some API 34/37 modules support interface targets from fixed sources without device validation yet. API 30–33, missing target-kind services or failed probes retain rejection 71. On arm64 Android 13–17, `RetransformClasses` routes final ClassFileLoadHook bytes through the enhanced structural transaction (`retransformRouted`); Android 11/12 retain ART rules. Validation failure falls back to ART, and routing does not call ClassFileLoadHook again. Cached raw field offsets are not protected. See [LIMITATIONS.en.md](docs/LIMITATIONS.en.md) for complete restrictions, rejection codes and semantics.

On Android 11–17 / API 30–37, interface targets support default/static/private body edits, new public abstract/default/static methods and private instance methods, deletion or signature changes of any method, abstract/default switches, static field changes, and added parent interfaces. They can share atomic batches with related implementations, subclasses or unrelated classes. Every transaction checks its conditions; even body-only edits rebuild affected instances while retaining compatible fields. API 33–36 are device-verified; API 30–32 and 37 are enabled from fixed sources and the named services of stored device libraries, pending device validation. See [redefining interfaces](docs/LIMITATIONS.en.md#redefining-interfaces).

## Quick start

The sample app is in [app](app), and a standalone user agent example is in [standard_jvmti_client.cc](app/src/debug/cpp/testing/standard_jvmti_client.cc).

### 1. Build and integrate

Integrate through the Maven dependency. **ShadowHook does not need to be declared separately:**

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
// app module build.gradle
dependencies {
    implementation 'io.github.ytlog:artdcevm:0.1.0-alpha.2'
}
```

The published metadata declares ShadowHook as a runtime dependency, so Gradle downloads and packages it automatically.

You can also build from source. This requires JDK 17 or later, Android SDK 37, NDK 27.2.12479018 and CMake 4.3.4, with `sdk.dir` set in your local `local.properties`:

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

The library's `minSdk` is 30 and it ships arm64-v8a native libraries. The app must enable `debuggable` from startup; debugging cannot be turned on after the process has started.

### 2. Initialize

Call this once, before your own agent redefines any class:

```java
import com.github.ytlog.artdcevm.api.DcevmRuntime;

// context is the app Context; throws with diagnostics if attaching or installing fails.
DcevmRuntime.initialize(context);
```

This loads `libartdcevm.so`, attaches the enhancement agent and takes over the standard JVMTI entry. Once initialization succeeds in a process, further calls return immediately.

`DcevmRuntime.capabilities()` returns a diagnostic snapshot; submission and error handling follow the contract below.

### 3. Attach your own JVMTI agent

ART DCEVM provides the initialization entry; loading patches and issuing redefinitions is up to the caller's agent. Your agent needs a JVMTI environment with the redefinition capability, for example:

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

After building and packaging your agent as a native library, attach it from the app:

```java
import android.os.Debug;

Debug.attachJvmtiAgent("libmy_agent.so", null, context.getClassLoader());
```

`libmy_agent.so` stands for your own library; it is not provided by this repository. Building an agent requires the JVMTI header; the sample uses [third_party/jvmti](third_party/jvmti).

### 4. Submit replacement classes

Android ART takes **DEX bytes**. Produce a new definition of the target class with the same class name, and pass the complete DEX data together with the currently loaded `jclass` to your agent:

```cpp
// target: the currently loaded target class.
// dex / dex_size: complete DEX data containing the new definition, and its length.
// The calling thread is attached to the VM; jvmti comes from your own agent.
jvmtiClassDefinition definition{};
definition.klass = target;
definition.class_byte_count = dex_size;
definition.class_bytes = dex;

jvmtiError error = jvmti->RedefineClasses(1, &definition);
if (error != JVMTI_ERROR_NONE) {
    // Return the code to the caller and read lastRejection (below); the batch did not take effect.
}
```

Classes that depend on each other should go into one `jvmtiClassDefinition[]` and be submitted as a single batch. Keep the class references and DEX buffers valid for the duration of the call. New classes and new lambda synthetic classes must first be provided through the loader; see [providing new classes](docs/LIMITATIONS.en.md#providing-new-classes) for the order.

## Error handling

After successful initialization, **submit standard `RedefineClasses` directly**. Return `0` (`JVMTI_ERROR_NONE`) means success. For a nonzero return, read the error code (JVMTI `GetErrorName` supplies its name) and `lastRejection`; do not treat the failed request as applied:

```java
// Read after the caller’s agent returns a nonzero error code.
String reason = DcevmRuntime.capabilities().optString("lastRejection", "");
```

`lastRejection` describes the latest standard-entry transaction failure and is cleared on success. Code 98 means the interface runtime is unavailable; restart the app process before retrying. See [LIMITATIONS.en.md](docs/LIMITATIONS.en.md) for other rejection rules and codes.

**`capabilities()` is diagnostic only, never a pre-submission admission check. Do not block submission based on its booleans or state strings; handle the actual transaction code and `lastRejection`.**

See the [engine design](docs/STRUCTURAL_REDEFINE_ENGINE.en.md), [public compatibility conclusions](docs/COMPATIBILITY.en.md), and [testing guide](docs/TESTING.en.md) for mechanisms, validation scope and build/test commands.
