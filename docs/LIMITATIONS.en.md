# Limitations

English | [中文](LIMITATIONS.md)

This document summarizes the prerequisites, rejected changes and runtime semantics. For implementation details, see [STRUCTURAL_REDEFINE_ENGINE.en.md](STRUCTURAL_REDEFINE_ENGINE.en.md).

## Prerequisites

- The app must be Java-debuggable from process start. ART does not expose `can_redefine_classes` in a release process, so the library cannot operate there.
- Only Android 11–17 on arm64-v8a is supported.
- Initialization fails if the runtime lacks any required base-entry symbol, or if the named wrapper does not match the public JVMTI table. Method and field retirement require additional symbols; missing symbols disable only the corresponding capabilities, with an explanation in `capabilities()`.
- Retirement requires runtime probes to confirm the ART class-layout declarations. A transaction whose probes fail cannot retire members; `memberMapping` in `capabilities()` gives the reason.
- The enhanced entry is the standard `RedefineClasses`. Batch inspection and execution through this entry are serialized. Concurrent transactions that call ART's structural redefinition extension directly do not participate in this protection.

## Redefining interfaces

Interface targets support body edits to default, static and private methods, new public abstract/default/static methods and private instance methods, and deletion or signature changes of static/private methods. Static additions may have any access level, including synthetic methods. Top-level and nested interfaces may share an atomic batch with implementations, their subclasses or unrelated classes, including multiple interfaces and parent/child interfaces; any failure preserves the whole batch. New classes, such as lambda synthetic classes, must first be appended by the reload tool, with changed existing synthetic classes redefined in the same batch.

This capability is available only on Android 13 (API 33) and Android 16 (API 36) arm64 devices. Other versions disable it with a reason in `capabilities()`. Runtime declarations must still pass probes; a version number alone does not establish availability.

`capabilities()` reads the latest probe state without running probes. For `interfaceRuntime`, `unavailable` means the interface runtime is unavailable; `disabled: reason` means it has been disabled, for example because the version, required services or runtime declarations do not match. Interface targets return 98 in either state. `unverified: …` means transaction-time probes have not completed, and `verified: …` means they have passed. After initialization and before an interface transaction completes the runtime probes, `interfaceRedefinition` is false. `unverified` does not mean unsupported and should not block the first transaction. Passing probes does not guarantee acceptance of every edit; the actual transaction result and `lastRejection` remain the final authority.

| Rejection for interface targets | JVMTI error code |
| --- | --- |
| Adding native or `<clinit>` methods; abstract additions without a valid implementation or with an unverified IfTable accessor; hiding an inherited interface/Object method with a direct method; a default shadowing static/non-public class-chain methods or introducing a default conflict | `METHOD_ADDED` (63) |
| Deleting or changing signatures of default/abstract methods | `METHOD_DELETED` (67) |
| Changing fields | `SCHEMA_CHANGED` (64) |
| Changing method modifiers | `METHOD_MODIFIERS_CHANGED` (71) |
| Changing parent interfaces | `HIERARCHY_CHANGED` (66) |
| Changing class modifiers | `CLASS_MODIFIERS_CHANGED` (70) |
| Unrelated interfaces share an implementation or another subtype, without a common interface ancestor in the batch | `HIERARCHY_CHANGED` (66) |
| An implementation's superclass outside the interface rebuild group changes structurally in the batch | `HIERARCHY_CHANGED` (66); body-only edits are accepted |
| A boot interface, a Proxy implementation, or a pointer-id marker on the target or any subtype | `UNMODIFIABLE_CLASS` (79) |
| Runtime version, required named services or declaration probes are unavailable | `NOT_AVAILABLE` (98) |

`lastRejection` names the rejected class and reason; failure preserves old behavior. A new default is available to existing instances, even when no implementation DEX was submitted; public class methods and more specific subinterfaces retain precedence. Static/private retirement uses the same bytecode, JNI, reflection and MethodHandle semantics as ordinary classes. Conflict rejections name the implementation and method. Inserting lambdas may renumber synthetic classes; the reload tool must isolate old closures or retain their old bridges, as for ordinary classes. See [interface member evolution](STRUCTURAL_REDEFINE_ENGINE.en.md#interface-member-evolution).

A new public abstract method requires **every loaded concrete implementation** in the interface group to dispatch through ART's actual IfTable to a public concrete instance method with the exact name and complete descriptor. This includes classes with no instances and no submitted DEX. A same-batch implementation may appear in either input order. Existing or inherited implementations and a more specific default actually selected by ART are accepted. Missing implementations, abstract/miranda targets, static/private/non-public targets and default conflicts reject the whole batch with 63 before commit, rather than deferring `AbstractMethodError` to invocation. Generic and covariant returns require real bridges; a name match or a different return descriptor is insufficient. Class-loader identities distinguish same-named types. `lastRejection` names the interface method and incomplete implementation; withdrawing or correcting the change permits retry.

The read-only IfTable accessor probes the Class field, interface method indices and arm64 PointerArray declaration. `interfaceAbstractDispatch` reports `unavailable` (optionally with a reason), `unverified: …`, `verified: …` or `rejected: reason`: the accessor is unavailable, dispatch probes have not completed, they have passed, or probes or required-service checks failed, respectively. While awaiting dispatch probes in an abstract-addition transaction, the state is `unverified: …` and `interfaceAbstractMethodAddition` is false; this also does not mean unsupported. That boolean is true only after the interface runtime and dispatch accessor pass their probes and required link-error services are available; each transaction still validates all implementations. If only the dispatch accessor is unavailable or rejected, abstract additions return 63; other interface edits remain subject to their own checks. ART 13 rejects non-public implementations before producing a usable IfTable. A named error service observes ART's actual selected pair; only errors matching pending obligations are converted to 63 through the original transaction.

A distant default cannot satisfy a more specific abstract redeclaration. Removing the last valid implementation in the same batch also rejects. Multiple interface groups commit only after all pass validation. Relevant types loaded after the snapshot trigger the existing bounded retries; exhaustion rejects the whole batch with 113, permitting a later corrected or withdrawn retry. Failure preserves old definitions, cached reflection/MethodHandle/JNI method IDs and warmed call sites.

Even a body-only interface edit reallocates every instance of all loaded implementations and their subclasses, preserves compatible fields, rewrites references and deoptimizes code. Its cost grows with affected instance count and heap size. Existing instances do not run their constructors again.

## Unsupported changes

These changes are rejected with a JVMTI error, leaving the old definitions intact.

| Change | Error code | Notes |
| --- | --- | --- |
| Switching a method between static and instance, making an instance method private, or re-adding a deleted method as another invoke kind | `UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED` (71) | Verified code keeps invoking that name and signature as the old kind; see [invoke kinds stay fixed](STRUCTURAL_REDEFINE_ENGINE.en.md#invoke-kinds-stay-fixed). Use a different name or signature instead |
| Changing a method's varargs or strict flags | `UNSUPPORTED_REDEFINITION_METHOD_MODIFIERS_CHANGED` (71) | |
| Switching a class between class, interface, annotation and enum kinds | `UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED` (70) | |
| Removing an interface, or changing the superclass in a way that drops an existing supertype | `UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED` (66) | Verified code depends on the old type relationships; see [why narrowing is unsupported](STRUCTURAL_REDEFINE_ENGINE.en.md#narrowing-is-unsupported) |

When retirement is unavailable, method deletion or signature changes are rejected with `UNSUPPORTED_REDEFINITION_METHOD_DELETED` (67), and field deletion, type changes or storage changes with `UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED` (64). Changing native to Java is rejected with 71. Adding an interface or changing the superclass is rejected with 66 when no probed class layout is available. Superclass changes also require verification redirection (`superclassWidening` in `capabilities()`).

## Semantic conflicts

The following conflicts reject the transaction:

- A method becomes final while a loaded subclass overrides it.
- A class becomes final while it has a loaded subclass (70).
- A class becomes abstract while instances of that class itself still exist (70). Instances of its subclasses do not prevent the change.
- An added interface cannot be resolved through the target's loader, is not an interface, or is inaccessible to the target (`JVMTI_ERROR_FAILS_VERIFICATION`, 62).
- The new superclass cannot be resolved, is an interface, is final, is inaccessible, or has a final method overridden by the new definition or a loaded subclass (62); the new superclass extends the target itself (`JVMTI_ERROR_CIRCULAR_CLASS_DEFINITION`, 61); it does not extend the original superclass (66, narrowing); or the same batch also redefines the target's superclass, a subclass, or a class in the new superclass chain (66).
- When retirement is unavailable, adding an instance method or changing its access level may displace a loaded subclass's copied default/miranda method.

## Superclass changes

- Only widening is supported: the new superclass must extend the original superclass. Existing instances keep the values of their own fields and fields in the original superclass chain. Fields from the new superclass start at zero; its constructor is not run on existing instances. The new superclass is loaded before redefinition, without initialization.
- Virtual methods dispatch through the new hierarchy. Overrides in the new superclass take effect immediately for existing instances. `super` calls in old method frames already executing continue to use the old hierarchy.
- Starting with ART 13, verification of the new class resolves its name to the old class. The engine redirects that lookup to the new class during verification. If a version's verifier uses another resolution path, redefinition is rejected with 62 and the old definition remains unchanged.

## Class modifiers

- Visibility, final, abstract, synthetic and the InnerClass flags of nested classes can change. The new class uses the new modifiers. Already-resolved call sites and type references remain resolved: ART does not repeat their access checks. New resolutions use the new modifiers.

## Adding interfaces

- Existing instances and instances of loaded subclasses immediately become instances of the new interface. The interface is loaded through the target's loader before redefinition, without initialization. Its static initialization runs when first needed, as usual; the target's loader becomes an initiating loader for the interface.

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

- Ordinary class structural redefinition retains ART's checks: arrays, primitive types, proxy classes, `java.lang.String`, mirror classes, `java.lang.Thread` and its subclasses cannot be redefined. A class cannot be redefined while its initializer is executing on the current thread. Classes with pointer-form JNI IDs cannot undergo structural changes.
- Raw field offsets cached by `sun.misc.Unsafe`, `Atomic*FieldUpdater` and similar APIs bypass ART's entry points. Any structural redefinition that changes the instance layout, including adding fields, invalidates those offsets.

## Process-wide effects

- When method retirement is enabled, the library wraps JNI calls and field access through JVMTI `SetJNIFunctionTable`. ART disables this process's Core Platform API checks when the table is overridden; those checks apply only to callers in platform modules.
- Before each standard-entry redefinition, the library loads every class referenced by the new definitions through the target's defining loader, without initialization. This prevents ART from first loading a subclass during the transaction and leaving it under the old class; see [class loading during transactions](STRUCTURAL_REDEFINE_ENGINE.en.md#class-loading-during-transactions). The engine enables JVMTI `ClassPrepare` events during the transaction. If a subclass of a target is still loaded during that interval, for example because ClassFileLoadHook introduces a reference or another thread loads it concurrently, the attempt fails before commit and is retried automatically. ClassFileLoadHook is called again on each attempt. Three consecutive failed attempts return `JVMTI_ERROR_INTERNAL`.

## General semantics

- Fields added to existing objects start at zero or `null`; their constructors are not run again. New objects execute the new constructors.
- Structural changes make ART walk the heap to migrate instances, deoptimize every thread's frames and clear JIT code. The cost grows with heap size, and code needs to warm up again afterwards.
- Patch delivery, signature verification and access control are the caller's responsibility. `RetransformClasses` is outside the enhanced scope.
