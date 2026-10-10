#include "platform/common/method_identity_calibration.h"
#include <array>
#include <thread>
#include "platform/common/readable_mapping_scope.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
using namespace artdcevm;
namespace {
JNIEnv* expected_env;
bool pending = false, leaf_exception = false;
jobject local_ref = nullptr;
std::array<void*, 3> native_methods{};
std::array<int, 3> handles{};
jboolean ExceptionCheck(JNIEnv*) { return pending ? JNI_TRUE : JNI_FALSE; }
jobjectRefType RefType(JNIEnv*, jobject object) { return object == local_ref ? JNILocalRefType : JNIGlobalRefType; }
void* Leaf(const abi::common::RunnableJniScope& scope, jobject method) {
  assert(scope.env == expected_env && scope.self && scope.vm);
  if (leaf_exception) pending = true;
  for (size_t i = 0; i < handles.size(); ++i)
    if (method == reinterpret_cast<jobject>(&handles[i])) return native_methods[i];
  return nullptr;
}
constexpr uint32_t kIndices[] = {7, 13, 21};
constexpr uint32_t kFlags[] = {1, 0x19, 0x102};
std::array<method_identity::Oracle, 3> Oracles() {
  std::array<method_identity::Oracle, 3> result{};
  for (size_t i = 0; i < result.size(); ++i)
    result[i] = {reinterpret_cast<jobject>(&handles[i]), kIndices[i], kFlags[i]};
  return result;
}
void Write(unsigned char* method, bool old, uint32_t index, uint32_t flags, bool ambiguous = false) {
  std::memset(method, 0, old ? 40 : 32);
  std::memcpy(method + 4, &flags, 4);
  uint32_t other = ambiguous ? index : 0x00123456;
  std::memcpy(method + (old ? 8 : 12), &other, 4);
  std::memcpy(method + (old ? 12 : 8), &index, 4);
}
void Fails(const runnable_jni::Context& context,
           const std::array<method_identity::Oracle, 3>& oracles,
           abi::common::FromReflectedMethod leaf = Leaf) {
  method_identity::Calibration result{method_identity::Model::kMethod11, {{99, 88}}, {77}};
  std::string error;
  assert(!method_identity::Calibrate(context, leaf, oracles.data(), oracles.size(), &result, &error));
  assert(!error.empty() && result.model == method_identity::Model::kMethod11);
  assert(result.identities.size() == 1 && result.identities[0].access_flags == 99 &&
         result.identities[0].dex_method_index == 88 && result.readable_extents == std::vector<size_t>{77});
}
}
int main() {
  JNINativeInterface_ table{};
  table.ExceptionCheck = ExceptionCheck;
  table.GetObjectRefType = RefType;
  JNIEnv env{&table}; expected_env = &env;
  int self, vm;
  runnable_jni::Context context{&self, &env, &vm};
  const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  auto* bytes = static_cast<unsigned char*>(mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
                                                MAP_PRIVATE | MAP_ANON, -1, 0));
  assert(bytes != MAP_FAILED);
  assert(mprotect(bytes + page, page, PROT_NONE) == 0);
  // Actual OS mapping queries are reused only within a traversal. Renewed
  // scopes observe protection changes; independent threads never share them.
  size_t extent=0;
  {
    method_identity::ScopedReadableMappings scope;
    for(int i=0;i<10000;++i)assert(method_identity::ReadableMappingExtent(bytes+page-16,&extent)&&extent==16);
    assert(scope.OsQueries()==1);
    assert(!method_identity::ReadableMappingExtent(bytes+page,&extent));
    assert(!method_identity::ReadableMappingExtent(nullptr,&extent));
    assert(!method_identity::ReadableMappingExtent(bytes,nullptr));
    const auto queries=scope.OsQueries();
    std::thread independent([&]{method_identity::ScopedReadableMappings child;
      assert(method_identity::ReadableMappingExtent(bytes,&extent));assert(child.OsQueries()==1);});independent.join();
    {method_identity::ScopedReadableMappings nested;assert(method_identity::ReadableMappingExtent(bytes,&extent));assert(nested.OsQueries()==1);}
    assert(method_identity::ReadableMappingExtent(bytes,&extent)&&scope.OsQueries()==queries);
  }
  assert(mprotect(bytes,page,PROT_NONE)==0);
  {method_identity::ScopedReadableMappings renewed;assert(!method_identity::ReadableMappingExtent(bytes,&extent));}
  assert(mprotect(bytes,page,PROT_READ|PROT_WRITE)==0);
  {method_identity::ScopedReadableMappings renewed;assert(method_identity::ReadableMappingExtent(bytes,&extent));}
  auto oracles = Oracles();
  for (bool old : {false, true}) {
    for (size_t i = 0; i < 3; ++i) {
      native_methods[i] = bytes + i * 64;
      Write(static_cast<unsigned char*>(native_methods[i]), old, kIndices[i], kFlags[i] | 0x04000000u);
    }
    method_identity::Calibration result{};
    std::string error = "stale";
    assert(method_identity::Calibrate(context, Leaf, oracles.data(), 3, &result, &error));
    assert(error.empty() && result.model == (old ? method_identity::Model::kMethod11 : method_identity::Model::kMethod12Onward));
    for (size_t i = 0; i < 3; ++i) {
      assert(result.identities[i].dex_method_index == kIndices[i]);
      assert(result.identities[i].access_flags == (kFlags[i] | 0x04000000u));
      assert(result.readable_extents[i] >= (old ? 40u : 32u));
    }
    auto wrong = oracles; wrong[1].dex_method_index = 42; Fails(context, wrong);
    wrong = oracles; wrong[0].java_modifiers = 2; Fails(context, wrong);
    wrong = oracles; wrong[1].dex_method_index = wrong[0].dex_method_index; Fails(context, wrong);
    wrong = oracles; wrong[2].java_modifiers = 2; Fails(context, wrong);
  }
  for (size_t i = 0; i < 3; ++i)
    Write(static_cast<unsigned char*>(native_methods[i]), false, kIndices[i], kFlags[i], true);
  Fails(context, oracles);  // Both index offsets match: never choose by SDK.
  native_methods[0] = bytes + page - 32;
  Write(static_cast<unsigned char*>(native_methods[0]), false, kIndices[0], kFlags[0]);
  for (size_t i = 1; i < 3; ++i)
    Write(static_cast<unsigned char*>(native_methods[i]), false, kIndices[i], kFlags[i]);
  method_identity::Calibration boundary{};
  assert(method_identity::Calibrate(context, Leaf, oracles.data(), 3, &boundary, nullptr));
  assert(boundary.model == method_identity::Model::kMethod12Onward && boundary.readable_extents[0] == 32);
  native_methods[0] = bytes + page - 16; Fails(context, oracles);
  native_methods[0] = bytes + page; Fails(context, oracles);  // PROT_NONE: no speculative read.
  native_methods[0] = reinterpret_cast<void*>(0x12340); Fails(context, oracles);
  native_methods[0] = native_methods[1]; Fails(context, oracles);
  native_methods[0] = bytes; Write(bytes, false, kIndices[0], kFlags[0]);
  local_ref = oracles[0].strong_global_method; Fails(context, oracles); local_ref = nullptr;
  pending = true; Fails(context, oracles); assert(pending); pending = false;
  leaf_exception = true; Fails(context, oracles); assert(pending); leaf_exception = false; pending = false;
  Fails(context, oracles, nullptr);
  Fails({nullptr, &env, &vm}, oracles);
  assert(!method_identity::Calibrate(context, Leaf, oracles.data(), 2, &boundary, nullptr));
  size_t unchanged = 41;
  assert(!method_identity::ReadableMappingExtent(bytes + page, &unchanged) && unchanged == 41);
  assert(!method_identity::ReadableMappingExtent(nullptr, &unchanged));
  assert(!method_identity::ReadableMappingExtent(bytes, nullptr));
  assert(munmap(bytes, page * 2) == 0);
  puts("Method identity: real readable mappings/guard page, both declarations, independent indices/modifiers, ambiguity, roots and exception preservation passed");
}
