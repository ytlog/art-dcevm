#pragma once
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include <cstdint>
namespace artdcevm::abi::arm64::android_common {
// Fixed openjdkjvmti/ti_redefine.cc declaration in android-{11,12,13,14,15,16,17}.0.0_r1:
// Handle<T> is an opaque slot argument; release ObjPtr<T> is one pointer word.
// Presence proves only callable naming, never handle/root or allocation safety.
using HierarchyAllocator=android11::ReleaseObjectPointer (*)(void* owner,
    void* old_class_handle,void* selected_parent_handle,void* cache_handle,
    uint16_t dex_class_def_index);
// Independently audited ClassLinker::LoadClass declaration in all seven fixed
// source headers. Capture follows SetupClass/SetDexCache but precedes linking.
using HierarchyLoadClass=void(*)(void* linker,void* self,const void* dex_file,const void* class_def,const void* class_handle);
inline constexpr char kHierarchyLoadClass[]="_ZN3art11ClassLinker9LoadClassEPNS_6ThreadERKNS_7DexFileERKNS_3dex8ClassDefENS_6HandleINS_6mirror5ClassEEE";
inline constexpr char kHierarchyAllocator[]="_ZN12openjdkjvmti9Redefiner17ClassRedefinition22AllocateNewClassObjectEN3art6HandleINS2_6mirror5ClassEEES6_NS3_INS4_8DexCacheEEEt";
inline constexpr char kFinishSubtreeAllocations[]="_ZN12openjdkjvmti9Redefiner17ClassRedefinition25FinishNewClassAllocationsERNS_22RedefinitionDataHolderEPNS_20RedefinitionDataIterE";
inline constexpr char kCaptureLinkedClass[]="_ZN3art6mirror5Class22EnsureInstanceFieldIdsENS_6HandleIS1_EE";
}
