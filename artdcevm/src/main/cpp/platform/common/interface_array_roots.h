#pragma once
#include <jni.h>
#include <memory>

namespace artdcevm::interface_redefinition {
// Created and destroyed while Native. ART's global-reference table keeps both
// arrays live across allocation, suspension and reference replacement.
class ClassArrayRoots {
 public:
  // Fixed java_vm_ext.h/cc, Android 11--17: JavaVMExt derives from JavaVM;
  // DecodeGlobal(IndirectRef) returns ObjPtr<Object> under the mutator lock.
  using DecodeGlobal = void* (*)(JavaVM*, jobject);
  static std::unique_ptr<ClassArrayRoots> Create(
      JNIEnv* jni, JavaVM* vm, jclass element, jsize length) {
    auto roots = std::unique_ptr<ClassArrayRoots>(new ClassArrayRoots(jni, vm));
    for (auto& reference : roots->references_) {
      jobjectArray array = jni->NewObjectArray(length, element, nullptr);
      if (array) {
        reference = jni->NewGlobalRef(array);
        jni->DeleteLocalRef(array);
      }
      if (!reference) {
        if (jni->ExceptionCheck()) jni->ExceptionClear();
        return nullptr;
      }
    }
    return roots;
  }
  ~ClassArrayRoots() {
    for (jobject reference : references_)
      if (reference) jni_->DeleteGlobalRef(reference);
  }
  ClassArrayRoots(const ClassArrayRoots&) = delete;
  ClassArrayRoots& operator=(const ClassArrayRoots&) = delete;
  // Called only in ART's Runnable allocation phase, with the mutator lock.
  // Each decode reads ART's current root; no raw object survives a suspension.
  void* Old(DecodeGlobal decode) const { return decode(vm_, references_[0]); }
  void* New(DecodeGlobal decode) const { return decode(vm_, references_[1]); }

 private:
  ClassArrayRoots(JNIEnv* jni, JavaVM* vm) : jni_(jni), vm_(vm) {}
  JNIEnv* jni_;
  JavaVM* vm_;
  jobject references_[2]{};
};
}  // namespace artdcevm::interface_redefinition
