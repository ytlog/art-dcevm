# ART DCEVM

English | [中文](README.zh-CN.md)

ART DCEVM is a class redefinition enhancement library for the Android runtime. It replaces Java method implementations while an app is running and lets you change fields, methods, modifiers and the class hierarchy. It is intended for developing and debugging your own apps, hot-reload tooling and class evolution experiments.

The app must be debuggable from process start. No root and no system partition changes are required. After initialization, callers submit redefinitions through the standard `jvmtiEnv::RedefineClasses` from their own JVMTI agent.

## Features

- **Method body replacement**: update method implementations of loaded classes.
- **Adding and removing members**: add, remove and re-add fields, methods and constructors, including native and abstract methods and class initializers.
- **Member shape changes**: change field types and method signatures, switch fields between static and instance, and switch methods between native and Java or abstract and concrete.
- **Modifier changes**: change access, final, abstract, synchronized, volatile and other modifiers on members and classes; an instance method cannot become private.
- **Hierarchy evolution**: add interfaces, and replace the superclass with a subclass of the original superclass.
- **Live objects**: existing values of compatible fields are kept; fields added to existing objects start as zero or `null`, and constructors are not re-run.
- **Stale reference protection**: removed or replaced members reject access through bytecode, reflection, JNI, MethodHandle and VarHandle.
- **Batch redefinition**: submit several classes in one `RedefineClasses` call; unsupported changes are rejected with a JVMTI error, nothing in the batch takes effect and the old definitions stay usable.

Instance migration, reference rewriting, deoptimization and JIT invalidation are all performed by the structural redefinition transaction built into the device's ART. This library only takes over its verification, member mapping and class allocation decisions; see the [engine design](docs/STRUCTURAL_REDEFINE_ENGINE.en.md).

Ordinary-class member retirement also requires a Native-phase probe of the current ART Thread declaration and named read-barrier services. `memberMapping` may therefore reject because of Thread probing, as well as Class layout probing; an unavailable mapping keeps its original installation reason. The Thread probe has physical-device coverage on Android 11/API 30, Android 12/API 31, 12L/API 32, Android 13/API 33, Android 14/API 34, Android 15/API 35, Android 16/API 36 and Android 17/API 37. Android 14 and 15 cover both CC and CMC; Android 17 covers generational CMC, with CC still pending.

## Interface redefinition

Android 13 (API 33), Android 14 (API 34), Android 15 (API 35) and Android 16 (API 36) arm64 devices support body edits to default, static and private methods of top-level and nested interfaces, new public abstract/default/static methods and private instance methods, and deletion or signature changes of static/private methods (including synthetic lambda helpers and bridges). Static additions may have any access level. Interfaces may share an atomic batch with implementations, their subclasses or unrelated classes, including multiple interfaces and parent/child interfaces. Other Android versions disable the capability with a reason; runtime probes must still pass.

Adding native/`<clinit>` methods, unsafe inherited-method hiding or new default conflicts returns 63. A new public abstract method requires every loaded concrete implementation to dispatch legally, with missing implementations or unavailable dispatch probes also returning 63. Deleting or changing default/abstract signatures returns 67; field changes return 64, method-modifier changes 71, and parent-interface changes 66. New defaults reach existing instances and implementations without submitted DEX. For lambda changes, the reload tool must append new classes and submit changed existing synthetic classes and implementation bridges with the interface. Unrelated interfaces sharing an implementation without a common batch interface ancestor, and structural superclass changes outside the interface rebuild group, return 66. Proxy implementations, boot interfaces and pointer-id markers return 79; interface targets return 98 when `interfaceRuntime` is `unavailable` or `disabled: reason`.

Even a body-only interface edit reallocates all instances of implementations and their subclasses, preserving compatible fields.

Read `DcevmRuntime.capabilities()` for probe status during integration; reading it does not run probes. For `interfaceRuntime`, `unavailable` means the interface runtime is unavailable, `disabled: reason` means it has been disabled, `unverified: …` means transaction-time probes have not completed, and `verified: …` means they have passed. `interfaceRedefinition` remains false until an interface transaction completes the runtime probes. `interfaceAbstractDispatch` separately reports dispatch probes for abstract additions: `unverified: …` while awaiting the relevant transaction probes, or `unavailable` (optionally with a reason), `verified: …` or `rejected: reason`. The latter means dispatch probes or required-service checks failed, rejecting abstract additions with 63. `interfaceAbstractMethodAddition` remains false until both runtime and dispatch probes pass and the required link-error services are available. `unverified` does not mean unsupported and should not block the first transaction. Use the actual transaction result and `lastRejection` as the final authority. See [limitations](docs/LIMITATIONS.en.md#redefining-interfaces) for the full scope and error semantics.

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

`DcevmRuntime.capabilities()` returns a `JSONObject` with the supported change categories, diagnostics and the reason for the most recent rejection.

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
    // Log the error code and handle the failure; the request did not take effect.
}
```

Classes that depend on each other should go into one `jvmtiClassDefinition[]` and be submitted as a single batch. Keep the class references and DEX buffers valid for the duration of the call. The sample app's [patch sources](fixtures) and [DEX build script](tools/build-fixtures.py) show how test patches are produced.

### 5. Add new classes

`RedefineClasses` replaces loaded classes only. Classes that the APK lacks (new classes and synthetic ones such as new lambdas) reach the app through its class loader, supplied by the tool; this library does not define new classes, the same split as DCEVM and hot-reload tools on Java. For one update, the tool:

1. Compiles each class to its own DEX (D8 `--file-per-class` emits a new lambda's synthetic class separately).
2. Classifies with `Class.forName(name, false, appClassLoader)`: a class it finds exists, and an existing class nothing has used yet is loaded this way so it can be redefined; a class it cannot find is new.
3. Packs the new classes into one jar, marks it read-only (since Android 14, apps targeting SDK 34 or higher cannot load writable DEX files) and appends it to the end of the app loader's (`BaseDexClassLoader`) path with ART's JVMTI extension `com.android.art.classloader.add_to_dex_class_loader`.
4. Submits all existing classes in one `RedefineClasses` call. The library loads the appended classes the new definitions name, including new subclasses of redefined classes, before the transaction starts.

Appending only adds to the end of the loader's path, cannot be undone and does not override loaded classes; a class added this way is redefined like any existing class when it changes again. A new class's static initializer runs on first use; a redefined class's static initializer does not run again. See [HotReloadTool.java](app/src/debug/java/com/github/ytlog/artdcevm/testing/HotReloadTool.java) for a sample implementation.

## Notes

- Fields added to existing objects start as zero or `null`; newly created objects run the new constructors. A change of field type or of static / instance form retires the old member and creates a new one.
- A removed member, or one whose signature or type changed, is retired: old references to it throw `NoSuchMethodError`, `NoSuchFieldError` and similar errors; method frames already executing keep running the old body. Application code must handle these semantic changes.
- A method keeps its invoke kind: switching it between static and instance, making an instance method private, or re-adding a deleted method as the other kind is rejected, because verified callers would still invoke it as the old kind.
- Removing an interface, or any hierarchy change that drops an existing supertype, is rejected. Conflicts such as a loaded subclass overriding a method that becomes final, or a class with its own live instances becoming abstract, also reject the transaction.
- Structural changes walk the heap to migrate instances, deoptimize every thread's frames and clear JIT code. The cost grows with heap size, and code needs to warm up again afterwards.
- Raw field offsets cached by `Unsafe`, `Atomic*FieldUpdater` and similar APIs become invalid once the instance layout changes.
- `RetransformClasses` is not covered. Patch delivery, signature verification, access control and release tooling are the caller's responsibility.

Full semantics are described in [limitations](docs/LIMITATIONS.en.md).

## Sample and verification

```sh
# After building the sample APK, run the full device regression on a connected arm64 device
python3 tools/run-device-tests.py --serial DEVICE
```

The script installs and drives the sample app `dev.artdcevm.lab`, kills any previous process and starts from the initial class definitions. Raw reports are saved locally under `build/evidence/` and are not published with the repository.

## Documentation

- [Engine design](docs/STRUCTURAL_REDEFINE_ENGINE.en.md)
- [Limitations and semantics](docs/LIMITATIONS.en.md)
