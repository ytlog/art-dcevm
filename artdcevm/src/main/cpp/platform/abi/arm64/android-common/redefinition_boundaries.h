#pragma once
#include "platform/abi/arm64/android11/transaction_roots.h"
namespace artdcevm::abi::arm64::android_common::redefinition_boundaries {
// Fixed Android11–17 ti_redefine.h/cc declarations. The driver loops may be
// inlined; their Class leaves still carry the real native Holder and index.
// CheckVerification(const Iter&) is a pointer ABI, never a by-value aggregate.
using Whole = bool(*)(void* driver,android11::TransactionHolder*);
using VerifyClass = bool(*)(void* owner,const android11::TransactionIterator*);
using CollectClass = bool(*)(void* owner,android11::TransactionIterator*);
inline constexpr char kVerifyWhole[] = "_ZN12openjdkjvmti9Redefiner26CheckAllClassesAreVerifiedERNS_22RedefinitionDataHolderE";
inline constexpr char kCollectWhole[] = "_ZN12openjdkjvmti9Redefiner28CollectAndCreateNewInstancesERNS_22RedefinitionDataHolderE";
inline constexpr char kVerifyClass[] = "_ZN12openjdkjvmti9Redefiner17ClassRedefinition17CheckVerificationERKNS_20RedefinitionDataIterE";
inline constexpr char kCollectClass[] = "_ZN12openjdkjvmti9Redefiner17ClassRedefinition28CollectAndCreateNewInstancesEPNS_20RedefinitionDataIterE";
}
