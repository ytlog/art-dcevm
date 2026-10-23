#pragma once
#include <vector>
#include <jni.h>
#include <cstdint>
#include "platform/abi/arm64/shadow_frame_table.h"
#include "platform/common/art_hook.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/abi/arm64/common/runtime_thread.h"
#include "platform/abi/arm64/common/interface_runtime.h"

// Keep ART's invocation checks when verified code re-resolves an evolved method.
namespace artdcevm::invoke_kind {
bool Prepare(std::vector<Hook>* hooks);
void Installed(bool installed);
void ObserveClassLinker(void* linker);
// Monotonic: checked resolution also preserves valid calls after an aborted batch.
bool Enable();
bool Protecting();
// Installed obsolete-copy DEX ownership guard with verified Class/collector declarations.
bool CopiedFrames();
// Retained private invoke-direct frames require modern cache fill or legacy
// checked-access services tied to ART's retained Class history.
bool DirectSelectionServices();
bool PrivateDirectFrames();
const char* ContinuationIdentityService();
uint32_t DirectAccessProofs();
const char* DirectBoundary();
bool PrepareDirectBoundaryProbe();
// Caller supplies Native-probed Thread declarations and ART's existing admission root.
void ObserveScope(void* thread, const abi::arm64::RuntimeThread* thread_layout,
                  const abi::arm64::InterfaceRuntime* layout, abi::arm64::Handle target);
void ObserveClassLayout(size_t access_flags, size_t dex_cache, size_t ext_data, size_t obsolete_methods,
                        size_t gc_marking = ~size_t{0}, size_t method_index = ~size_t{0}, size_t iftable = ~size_t{0},
                        abi::arm64::FrameReference frame_reference = nullptr, size_t exception = ~size_t{0}, abi::arm64::FramePc frame_pc = nullptr, abi::arm64::FrameReturn frame_return = nullptr);
// Class and interface selectors are independent. The no-argument form is the
// compatibility capability requiring both; admission uses the target's kind.
// Each transaction still needs the Class/Thread/HandleScope probes.
bool SelectionServices();
bool SelectionServices(bool interface_target);
bool SelfCalls();
bool SelfCalls(bool interface_target);
const char* SelectionUnavailable(bool interface_target);
// Probed Class/DexCache/collector declarations and named type lookup check
// the symbolic owner, including inherited defaults and Object methods.
bool ClassKinds();
// Commit visitor only: identify interface-owned DEX entries independently of
// their cached canonical ArtMethod. Reflection/JNI/MethodHandle identities stay mapped.
bool InterfaceResolution(void* cache, uint32_t index);
bool InterfaceSymbols();
// Runnable BindTo entry: receiver is rooted by ART's current ShadowFrame.
// Returns 0 to proceed, 1 for a pending exception, 2 for a completed capture.
// Direct dispatch stays native; an incompatible capture feeding an adapter
// continues through rooted JNI/libcore calls before ART returns the void frame.
int CheckBoundReceiver(void* self, void* bound);
// Install the post-adaptation MethodHandle guard before accepting the transaction.
bool EnableInterfaceSymbols();
const char* ClassKindUnavailable();
bool EnableClassKinds();
bool ClassKindProtection();
// Retired constructors still check current class instantiability before the
// missing body, as reflection, MethodHandle and JNI construction do on JBR.
bool UninstantiableConstructor(const void* method);
// Commit under ART suspend-all/JIT suspension; IsCompilable gates both JIT and inlining.
void KeepInterpreted(void* method, uint32_t precompiled_flag);
}  // namespace artdcevm::invoke_kind
