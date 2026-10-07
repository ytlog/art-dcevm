#include "platform/android13/class_validation.h"
#include "platform/android13/class_validation_abi.h"
#include "platform/android13/redefinition_art_abi.h"
#include "platform/android13/redefinition_failure.h"
#include "platform/android13/validation_hooks.h"
#include "platform/android13/validation_metadata.h"
#include "evolution/class_schema.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace artdcevm;
namespace {
jvmtiEnv* current_env;
JNIEnv* current_jni;
evolution::ClassSchema live;
uint32_t raw_flags;
int allocated = 0, locals = 0, failure_code = 0, markers = 0, hierarchy_code = 0;
bool subclass_valid = true, hierarchy_changed = false;
std::string failure_message;
android13::ClassValidation callback;
void* Alloc(size_t size) { ++allocated; return std::malloc(size); }
jvmtiError JNICALL Free(jvmtiEnv*, unsigned char* bytes) { --allocated; std::free(bytes); return JVMTI_ERROR_NONE; }
jvmtiError JNICALL Signature(jvmtiEnv*, jclass value, char** result, char**) {
  auto index = reinterpret_cast<uintptr_t>(value);
  std::string name = index == 1 ? live.descriptor : index == 2 ? live.superclass : live.interfaces.at(index - 100);
  *result = static_cast<char*>(Alloc(name.size() + 1)); std::strcpy(*result, name.c_str());
  return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Modifiers(jvmtiEnv*, jclass, jint* result) {
  // InnerClass annotations may add Java visibility bits absent in actual Class access flags.
  *result = static_cast<jint>(live.access | 0x0002); return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Interfaces(jvmtiEnv*, jclass, jint* count, jclass** result) {
  *count = static_cast<jint>(live.interfaces.size());
  *result = static_cast<jclass*>(Alloc(live.interfaces.size() * sizeof(jclass)));
  for (size_t i = 0; i < live.interfaces.size(); ++i) { (*result)[i] = reinterpret_cast<jclass>(100 + i); ++locals; }
  return JVMTI_ERROR_NONE;
}
jclass JNICALL Super(JNIEnv*, jclass) { if (live.superclass.empty()) return nullptr; ++locals; return reinterpret_cast<jclass>(2); }
void JNICALL Delete(JNIEnv*, jobject) { --locals; }
jboolean JNICALL Exception(JNIEnv*) { return JNI_FALSE; }
void Mark(void*) { ++markers; }
class Hooks : public HookEngine {
 public:
  bool Initialize(std::string*) override { return true; }
  void* FindSymbol(const char* library, const char* name) override {
    assert(std::string(library) == "libopenjdkjvmti.so");
    assert(std::string(name) == "_ZN12openjdkjvmti9Redefiner17ClassRedefinition20RecordNewMethodAddedEv");
    return reinterpret_cast<void*>(Mark);
  }
  void* Install(void*, void*, void**, std::string*) override { std::abort(); }
  bool Remove(void*, std::string*) override { std::abort(); }
};
uint32_t U32(const std::vector<unsigned char>& bytes, size_t off) {
  uint32_t value; std::memcpy(&value, bytes.data() + off, sizeof(value)); return value;
}
void Put(std::vector<unsigned char>& bytes, size_t off, uint32_t value) { std::memcpy(bytes.data() + off, &value, sizeof(value)); }
void Checksum(std::vector<unsigned char>& bytes) {
  uint32_t a = 1, b = 0;
  for (size_t i = 12; i < bytes.size(); ++i) { a = (a + bytes[i]) % 65521; b = (b + a) % 65521; }
  Put(bytes, 8, (b << 16) | a);
}
}
namespace artdcevm::android13 {
jvmtiEnv* ValidationHooks::TransactionEnvironment() { return current_env; }
JNIEnv* ValidationHooks::TransactionJni() { return current_jni; }
void RecordRedefinitionFailure(void*, int code, const char* message) { failure_code = code; failure_message = message; }
bool ReadValidationMetadata(void*, ValidationMetadata* output) {
  output->access_flags = raw_flags;
  return true;
}
}
extern "C" bool ArtDcevmSourceClassSubclassGuard(void*) { return subclass_valid; }
extern "C" int ArtDcevmSourceClassHierarchyGuard(void*, bool* changed, const char** error) {
  *changed = hierarchy_changed; *error = "Typed hierarchy guard diagnostic"; return hierarchy_code;
}
extern "C" void ArtDcevmSourceConfigureClassValidation(android13::ClassValidation value) { callback = value; }
int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream input(argv[1], std::ios::binary);
  assert(input.good());
  std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  std::string error;
  assert(evolution::ReadDexDeclarations(bytes, &live, &error)); raw_flags = live.access & 0x5431;
  jvmtiInterface_1_ jvmti_table{}; jvmti_table.Deallocate = Free;
  jvmti_table.GetClassSignature = Signature; jvmti_table.GetClassModifiers = Modifiers;
  jvmti_table.GetImplementedInterfaces = Interfaces;
  jvmtiEnv env{&jvmti_table}; current_env = &env;
  JNINativeInterface_ jni_table{}; jni_table.GetSuperclass = Super;
  jni_table.DeleteLocalRef = Delete; jni_table.ExceptionCheck = Exception;
  JNIEnv jni{&jni_table}; current_jni = &jni;
  Hooks hooks;
  void* implementation = dlopen(nullptr, RTLD_NOW);
  assert(implementation);
  if (!android13::ConfigureEvolutionClassValidation(hooks, implementation, &error)) {
    std::cerr << error << ": " << dlerror() << "\n"; return 1;
  }
  android13::redefinition_abi::DexFilePrefix dex{nullptr, bytes.data(), bytes.size()};
  android13::redefinition_abi::ClassRedefinition definition{};
  definition.klass = reinterpret_cast<jclass>(1); definition.dex_file = &dex; definition.lock_acquired = true;
  auto run = [&] { failure_code = 0; markers = 0; const bool result = callback(&definition); assert(allocated == 0 && locals == 0); return result; };
  assert(run() && markers == 0);  // Public InnerClass modifiers must not change ART class validation.
  definition.lock_acquired = false; assert(!run() && failure_code == JVMTI_ERROR_INTERNAL);
  definition.lock_acquired = true;
  const auto descriptor = live.descriptor; live.descriptor = "Lother/Class;";
  assert(!run() && failure_code == JVMTI_ERROR_NAMES_DONT_MATCH); live.descriptor = descriptor;
  hierarchy_code = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
  assert(!run() && failure_code == hierarchy_code && failure_message == "Typed hierarchy guard diagnostic"); hierarchy_code = 0;
  hierarchy_changed = true; assert(!run() && failure_code == JVMTI_ERROR_INTERNAL); hierarchy_changed = false;
  Put(bytes, U32(bytes, 100) + 4, live.access | 0x10); Checksum(bytes);
  subclass_valid = false;
  assert(!run() && failure_code == JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED);
  subclass_valid = true; assert(run() && markers == 1);
  Put(bytes, 96, 2); Checksum(bytes); assert(!run() && failure_code == JVMTI_ERROR_ILLEGAL_ARGUMENT);
  std::cout << "class validation: own DEX/metadata, lock/name failures, raw class flags, final guard, hierarchy diagnostics and ownership passed\n";
}
