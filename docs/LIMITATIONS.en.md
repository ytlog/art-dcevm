# Limitations

English | [中文](LIMITATIONS.md)

This document summarizes the prerequisites, rejected changes and runtime semantics. For implementation details, see [STRUCTURAL_REDEFINE_ENGINE.en.md](STRUCTURAL_REDEFINE_ENGINE.en.md).

## Submission and error handling

After successful initialization, submit the complete batch directly through the caller agent’s standard `jvmtiEnv::RedefineClasses`. Return `0` (`JVMTI_ERROR_NONE`) means success. Any nonzero return means failure: handle the returned error code and read `lastRejection` from `DcevmRuntime.capabilities()`; old definitions remain unchanged. `lastRejection` describes the latest standard-entry transaction failure and is cleared on success.

`capabilities()` is diagnostic only, never a pre-submission admission check. Do not block submission using its booleans or status strings. Actual transaction checks decide admission and supported changes. See [testing](TESTING.en.md#diagnostic-report-reference) for the diagnostic fields.

## Prerequisites

- The app must be Java-debuggable from process start. ART does not expose `can_redefine_classes` in a release process, so the library cannot operate there.
- Only Android 11–17 on arm64-v8a is supported.
- Initialization fails if the runtime lacks any required base-entry symbol, or if the named wrapper does not match the public JVMTI table. Method and field retirement require additional symbols; missing symbols disable only the corresponding capabilities, with an explanation in `capabilities()`.
- Retirement requires runtime probes to confirm the ART class-layout declarations. A transaction whose probes fail cannot retire members; `memberMapping` in `capabilities()` gives the reason.
- The enhanced entry is the standard `RedefineClasses`. Batch inspection and execution through this entry are serialized. Concurrent transactions that call ART's structural redefinition extension directly do not participate in this protection.

Member retirement also requires Native-phase thread and named read-barrier checks. Failed thread or class-layout checks reject under the existing error rules. See [compatibility](COMPATIBILITY.en.md) for device coverage and pending releases.

## Redefining interfaces

Interface targets support body edits to default, static and private methods, new public abstract/default/static methods and private instance methods, deletion or signature changes of any method including default/abstract ones, switching an existing public instance method between abstract and default, changing a method's invoke kind among default/abstract, static and private (same rules as ordinary classes; see 71 below), adding, deleting or retyping static fields, and adding parent interfaces. Static additions may have any access level, including synthetic methods. Top-level and nested interfaces may share an atomic batch with implementations, their subclasses or unrelated classes, including multiple interfaces and parent/child interfaces; any failure preserves the whole batch. New classes, such as lambda synthetic classes, must first be appended by the reload tool, with changed existing synthetic classes redefined in the same batch.

This capability is available only on Android 13 (API 33), Android 14 (API 34), Android 15 (API 35) and Android 16 (API 36) arm64 devices. Other versions disable it with a reason in `capabilities()`. Runtime declarations must still pass probes; a version number alone does not establish availability.

| Rejection for interface targets | JVMTI error code |
| --- | --- |
| Adding native methods; abstract additions without a valid implementation or with an unverified IfTable accessor; hiding an inherited interface/Object method with a direct method; a default shadowing static/non-public class-chain methods or introducing a default conflict | `METHOD_ADDED` (63) |
| Deleting or changing method signatures without member mapping | `METHOD_DELETED` (67) |
| Field modifier changes; field additions, deletions or type changes without member mapping | `SCHEMA_CHANGED` (64) |
| Interface method modifier changes other than abstract/default switches and invoke-kind changes; switching to abstract without a verified IfTable accessor; switching to default where it would shadow static/non-public class-chain methods | `METHOD_MODIFIERS_CHANGED` (71) |
| Removing a parent interface; adding one without member mapping; a static/private method hiding a new parent's method, or a new parent default shadowing static/non-public class-chain methods; a new parent's abstract methods without a verified IfTable accessor | `HIERARCHY_CHANGED` (66) |
| An added interface is rebuilt by the batch, and the new class and that rebuild depend on each other, or the new class precedes it within one rebuild | `HIERARCHY_CHANGED` (66) |
| Changing the kind between interface, annotation and enum (an interface's access level, synthetic and similar modifiers may change as a class's) | `CLASS_MODIFIERS_CHANGED` (70) |
| An implementation's superclass outside the interface rebuild group changes its own superclass in the batch | `HIERARCHY_CHANGED` (66) |
| A boot interface, active pointer JNI IDs, or a pointer-id marker on the target or any subtype | `UNMODIFIABLE_CLASS` (79) |
| An interface with Proxy implementations changing its parent interfaces | `HIERARCHY_CHANGED` (66) |
| Runtime version, required named services or declaration probes are unavailable | `NOT_AVAILABLE` (98) |

A deleted default/abstract method retires like an ordinary-class method: bytecode calls through the interface or an implementation, cached reflection, JNI and MethodHandle throw `NoSuchMethodError`, while a same-named method declared by an implementation stays. A signature change retires the old signature and adds the new one. Switching a default to abstract requires, like an abstract addition, a valid implementation in every loaded concrete implementation, otherwise 63; switching abstract to default gives implementations without their own declaration a copied default. The interface is initialized before redefinition, so an added static field keeps its default value; as for a class, an added or changed `<clinit>` never runs on an initialized interface; callers usually inline constant expressions. When a parent interface is added, the abstract methods of the new parent, and of its parents the target did not inherit, require like abstract additions a valid implementation in every loaded concrete implementation, otherwise 63 naming the new parent method and the class without it; their defaults reach implementations as copied defaults.

`lastRejection` names the rejected class and reason; failure preserves old behavior. A new default is available to existing instances, even when no implementation DEX was submitted; public class methods and more specific subinterfaces retain precedence. Static/private retirement uses the same bytecode, JNI, reflection and MethodHandle semantics as ordinary classes. Conflict rejections name the implementation and method. Inserting lambdas may renumber synthetic classes; the reload tool must isolate old closures or retain their old bridges, as for ordinary classes. See [interface member evolution](STRUCTURAL_REDEFINE_ENGINE.en.md#interface-member-evolution).

Interface transactions require JNI IDs for existing members that ART can rewrite. Plain pointer mode may lack ClassExt pointer-id markers, so the Native snapshot phase also checks the representation of a method or field ID returned by public JVMTI. A pointer ID rejects with 79 before entering the ART transaction. Merely switching modes cannot repair previously cached pointer IDs. Ordinary classes retain their existing admission rules.

A new public abstract method requires **every loaded concrete implementation** in the interface group to dispatch through ART's actual IfTable to a public concrete instance method with the exact name and complete descriptor. This includes classes with no instances and no submitted DEX. A same-batch implementation may appear in either input order. Existing or inherited implementations and a more specific default actually selected by ART are accepted. Missing implementations, abstract/miranda targets, static/private/non-public targets and default conflicts reject the whole batch with 63 before commit, rather than deferring `AbstractMethodError` to invocation. Generic and covariant returns require real bridges; a name match or a different return descriptor is insufficient. Class-loader identities distinguish same-named types. `lastRejection` names the interface method and incomplete implementation; withdrawing or correcting the change permits retry.

Abstract additions require actual dispatch validation on every transaction. An unavailable dispatch accessor or required link-error service rejects abstract additions with 63; other interface edits retain their own rules. ART 13 rejects non-public implementations before producing a usable IfTable; the library observes ART’s selected pair and converts only errors matching pending obligations to 63 through the original transaction. ART 14 also requires that named link-error service; ART 15 and 16 validate linked dispatch arrays.

A distant default cannot satisfy a more specific abstract redeclaration. Removing the last valid implementation in the same batch also rejects. Multiple interface groups commit only after all pass validation. Relevant types loaded after the snapshot trigger the existing bounded retries; exhaustion rejects the whole batch with 113, permitting a later corrected or withdrawn retry. Failure preserves old definitions, cached reflection/MethodHandle/JNI method IDs and warmed call sites.

Interface groups whose subtypes intersect, and a structurally edited class outside them that is a superclass of a member, merge into one rebuild that replaces and migrates each type once, independently of submission order. ART collects only instances of the owner entry's class; when no root covers every concrete member, the engine has ART collect again with each other root, keeping every instance only from the round of the first root it is assignable to. Unrelated interfaces sharing an implementation, or an interface and an outside superclass with separate instances, are therefore also migrated once. A body-only edit of such a superclass does not join the rebuild and is still updated in place.

Even a body-only interface edit reallocates every instance of all loaded implementations and their subclasses, preserves compatible fields, rewrites references and deoptimizes code. Its cost grows with affected instance count and heap size. Existing instances do not run their constructors again.


Classes generated by `java.lang.reflect.Proxy` (for example Retrofit services or annotation instances) keep their class and `InvocationHandler`. Kept methods still reach the handler, which receives the new interface's `Method`; a new abstract method the Proxy lacks throws `AbstractMethodError` when called, and a new default runs its body. Proxies created later for the same interface reuse that class and behave the same, as with JBR. A new abstract method is not rejected because a Proxy lacks it (a Proxy has no source to update in the same batch); ordinary implementations are still checked as described above.
## Unsupported changes

These changes are rejected with a JVMTI error, leaving the old definitions intact.

| Change | Error code | Notes |
| --- | --- | --- |
| Switching a method between static and instance, making an instance method private, or re-adding a deleted method as another invoke kind, while code of a loaded class outside the batch still names it with the old kind or a batch class has an active frame | `UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED` (71) | Verified code keeps invoking that name and signature as the old kind; see [invoke kind changes](STRUCTURAL_REDEFINE_ENGINE.en.md#invoke-kind-changes). Submit those callers in the same batch |
| Switching a class between class, interface, annotation and enum kinds | `UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED` (70) | |
| Changing the superclass in a way that drops an existing superclass | `UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED` (66) | Verified code depends on the old type relationships; see [why narrowing is unsupported](STRUCTURAL_REDEFINE_ENGINE.en.md#narrowing-is-unsupported) |
| An ordinary class removing an interface while it belongs to an interface group of the batch (it implements an interface the batch redefines, or is the superclass of a group member), or without member mapping or dispatch sealing | `UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED` (66) | See [removing interfaces](#removing-interfaces) |

When retirement is unavailable, method deletion or signature changes are rejected with `UNSUPPORTED_REDEFINITION_METHOD_DELETED` (67), and field deletion, type changes or storage changes with `UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED` (64). Changing native to Java is rejected with 71. Adding an interface or changing the superclass is rejected with 66 when no probed class layout is available. Superclass changes also require verification redirection.

## Semantic conflicts

The following conflicts reject the transaction:

- A method becomes final while a loaded subclass overrides it.
- A class becomes final while it has a loaded subclass (70).
- A class becomes abstract while instances of that class itself still exist (70). Instances of its subclasses do not prevent the change.
- An added interface cannot be resolved through the target's loader, is not an interface, or is inaccessible to the target (`JVMTI_ERROR_FAILS_VERIFICATION`, 62).
- The new superclass cannot be resolved, is an interface, is final, is inaccessible, or has a final method overridden by the new definition or a loaded subclass (62); the new superclass extends the target itself (`JVMTI_ERROR_CIRCULAR_CLASS_DEFINITION`, 61); it does not extend the original superclass (66, narrowing); or the same batch also structurally redefines a supertype of the target or a class in the new superclass chain, or a subclass that changes its superclass too (66); those supertypes may share the batch when they change only method bodies, and a subclass that only changes members may share it and verifies against the new superclass chain.
- When retirement is unavailable, adding an instance method or changing its access level may displace a loaded subclass's copied default/miranda method.

## Superclass changes

- Only widening is supported: the new superclass must extend the original superclass. Existing instances keep the values of their own fields and fields in the original superclass chain. Fields from the new superclass start at zero; its constructor is not run on existing instances. The new superclass is loaded before redefinition, without initialization.
- Virtual methods dispatch through the new hierarchy. Overrides in the new superclass take effect immediately for existing instances. `super` calls in old method frames already executing continue to use the old hierarchy.
- Starting with ART 13, verification of the new class resolves its name to the old class. The engine redirects that lookup to the new class during verification. If a version's verifier uses another resolution path, redefinition is rejected with 62 and the old definition remains unchanged.

## Class modifiers

- Visibility, final, abstract, synthetic and the InnerClass flags of nested classes can change. The new class uses the new modifiers. Already-resolved call sites and type references remain resolved: ART does not repeat their access checks. New resolutions use the new modifiers.

## Adding interfaces

- Existing instances and instances of loaded subclasses immediately become instances of the new interface. The interface is loaded through the target's loader before redefinition, without initialization. Its static initialization runs when first needed, as usual; the target's loader becomes an initiating loader for the interface.


## Removing interfaces

- An ordinary class may stop implementing an interface. Its existing instances and those of loaded subclasses immediately stop being instances of that interface (`instanceof`, `isInstance` and casts follow the new hierarchy).
- Verified code that still calls them as the removed interface, and old references stored in fields or arrays typed with it, throw `IncompatibleClassChangeError` when calling its methods, as on a JVM. This holds for interpreted code, JIT-compiled code and boot code (for example `Thread.run` calling `Runnable.run`). Old references are not cleared.
- Default methods the class inherited from the removed interface disappear: old bytecode that calls one on the class throws `NoSuchMethodError`.
- Interface calls on these classes are looked up through their dispatch slots and become slightly slower. Concrete subclasses loaded later inherit this, except a concrete subclass, loaded later, of an abstract class that lost the interface; see [removing interfaces](STRUCTURAL_REDEFINE_ENGINE.en.md#removing-interfaces) in the engine design.
- An interface target removing a parent interface is still rejected with 66.
## Method kinds

- A deleted native method is retired like any other method. Threads already executing it continue to do so. When a native method becomes Java, existing call sites, cached reflection objects and JNI method IDs execute the new Java body.
- When a method becomes abstract, calling it on a subclass instance whose class provides no implementation throws `AbstractMethodError`.
- Deleting a constructor makes bytecode `new` and JNI `NewObject*` throw `NoSuchMethodError`. A cached `Constructor.newInstance` throws `InvocationTargetException` wrapping that error.
- Deleting a class initializer does not affect an already-initialized class; static field values are retained.

## Retired members

- Deleted methods and the old versions of methods whose signatures change are retired. Bytecode, JNI and MethodHandle calls to them throw `NoSuchMethodError`; a cached reflection `Method.invoke` throws `InvocationTargetException` wrapping that error. Old method frames already executing continue to run the old body.
- Access to retired fields through bytecode, JNI or MethodHandle throws `NoSuchFieldError`. Cached reflection `Field` access throws `IllegalArgumentException` for instance fields and `NoClassDefFoundError` for static fields. VarHandle access throws `UnsupportedOperationException`. The name and declaring class of a cached reflection `Field` identify a field in a placeholder class inside the library.
- Restoring a member with the same name creates a new member. Previously cached references remain retired. A restored method keeps its earlier static or instance form, and a method that was virtual cannot come back as private.
- Classes that have retired members, and their class loaders, are kept alive until process exit.

## ART's own restrictions

- Ordinary class structural redefinition retains ART's checks: arrays, primitive types, proxy classes, `java.lang.String`, mirror classes, and `java.lang.Thread` itself and its supertypes cannot be redefined. Application subclasses of `Thread` can be: a started thread keeps its object identity, name, ID, interruption and join, and an executing `run()` frame continues the old body. A class cannot be redefined while its initializer is executing on the current thread. Classes with pointer-form JNI IDs cannot undergo structural changes.
- Raw field offsets cached by `sun.misc.Unsafe`, `Atomic*FieldUpdater` and similar APIs bypass ART's entry points. Any structural redefinition that changes the instance layout, including adding fields, invalidates those offsets.

## Process-wide effects

- When method retirement is enabled, the library wraps JNI calls and field access through JVMTI `SetJNIFunctionTable`. ART disables this process's Core Platform API checks when the table is overridden; those checks apply only to callers in platform modules.
- Before each standard-entry redefinition, the library loads every class referenced by the new definitions through the target's defining loader, without initialization. This prevents ART from first loading a subclass during the transaction and leaving it under the old class; see [class loading during transactions](STRUCTURAL_REDEFINE_ENGINE.en.md#class-loading-during-transactions). The engine enables JVMTI `ClassPrepare` events during the transaction. If a subclass of a target is still loaded during that interval, for example because ClassFileLoadHook introduces a reference or another thread loads it concurrently, the attempt fails before commit and is retried automatically. ClassFileLoadHook is called again on each attempt. Three consecutive failed attempts return `JVMTI_ERROR_INTERNAL`.

## Providing new classes

`RedefineClasses` replaces loaded classes only. Classes that the APK lacks (new classes and synthetic ones such as new lambdas) reach the app through its class loader, supplied by the tool; this library does not define new classes, the same split as DCEVM and hot-reload tools on Java. For one update, the tool:

1. Compiles each class to its own DEX (D8 `--file-per-class` emits a new lambda's synthetic class separately).
2. Classifies with `Class.forName(name, false, appClassLoader)`: a class it finds exists, and an existing class nothing has used yet is loaded this way so it can be redefined; a class it cannot find is new.
3. Packs the new classes into one jar, marks it read-only (since Android 14, apps targeting SDK 34 or higher cannot load writable DEX files) and appends it to the end of the app loader's (`BaseDexClassLoader`) path with ART's JVMTI extension `com.android.art.classloader.add_to_dex_class_loader`.
4. Submits all existing classes in one `RedefineClasses` call. The library loads the appended classes the new definitions name, including new subclasses of redefined classes, before the transaction starts.

Appending only adds to the end of the loader's path, cannot be undone and does not override loaded classes; a class added this way is redefined like any existing class when it changes again. A new class's static initializer runs on first use; a redefined class's static initializer does not run again. See [HotReloadTool.java](../app/src/debug/java/com/github/ytlog/artdcevm/testing/HotReloadTool.java) for a sample implementation.

## General semantics

- Fields added to existing objects start at zero or `null`; their constructors are not run again. New objects execute the new constructors.
- Structural changes make ART walk the heap to migrate instances, deoptimize every thread's frames and clear JIT code. The cost grows with heap size, and code needs to warm up again afterwards.
- Patch delivery, signature verification and access control are the caller's responsibility. `RetransformClasses` is outside the enhanced scope.
