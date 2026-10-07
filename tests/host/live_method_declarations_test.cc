#include "platform/android13/schema_snapshot.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
using namespace artdcevm;
namespace {
int allocations = 0, modifier_reads = 0;
bool fail_second = false;
void* Allocate(size_t size) { ++allocations; return std::malloc(size); }
char* String(const char* text) {
  auto* result = static_cast<char*>(Allocate(std::strlen(text) + 1));
  std::strcpy(result, text); return result;
}
jvmtiError JNICALL Deallocate(jvmtiEnv*, unsigned char* value) {
  --allocations; std::free(value); return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Methods(jvmtiEnv*, jclass, jint* count, jmethodID** output) {
  *count = 2;
  *output = static_cast<jmethodID*>(Allocate(2 * sizeof(jmethodID)));
  (*output)[0] = reinterpret_cast<jmethodID>(1);
  (*output)[1] = reinterpret_cast<jmethodID>(2);
  return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Name(jvmtiEnv*, jmethodID id, char** name, char** descriptor, char**) {
  *name = String(id == reinterpret_cast<jmethodID>(1) ? "<init>" : "value");
  *descriptor = String(id == reinterpret_cast<jmethodID>(1) ? "()V" : "()I");
  return fail_second && id == reinterpret_cast<jmethodID>(2)
      ? JVMTI_ERROR_INVALID_METHODID : JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Modifiers(jvmtiEnv*, jmethodID id, jint* flags) {
  ++modifier_reads;
  *flags = id == reinterpret_cast<jmethodID>(1) ? 1 : 0x21;
  return JVMTI_ERROR_NONE;
}

}
int main() {
  jvmtiInterface_1_ table{};
  table.Deallocate = Deallocate; table.GetClassMethods = Methods;
  table.GetMethodName = Name; table.GetMethodModifiers = Modifiers;
  jvmtiEnv env{&table};
  std::vector<evolution::Member> output;
  std::string error;
  assert(android13::ReadLiveMethods(&env, nullptr, &output, &error) == JVMTI_ERROR_NONE);
  assert(output.size() == 2 && output[0].name == "<init>" && output[1].descriptor == "()I");
  assert(output[0].access == 1 && output[1].access == 0x21 && modifier_reads == 2);
  assert(allocations == 0);
  fail_second = true;
  assert(android13::ReadLiveMethods(&env, nullptr, &output, &error) == JVMTI_ERROR_INVALID_METHODID);
  assert(output.size() == 2 && output[1].access == 0x21 && !error.empty());
  assert(allocations == 0);
  std::cout << "live method declarations: public planning flags, atomic failure and allocation cleanup passed\n";
}
