#include "platform/android13/allocator_membership.h"
#include "platform/android13/mutex_abi.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/runtime_access.h"
#include "platform/symbols/android13.h"
#include <cstddef>
#include <jvmti.h>
#include <vector>

namespace artdcevm::android13 {
namespace {
// Android 13 release declarations: Mutex precedes ArenaAllocator. Its stats
// and memory-tool bases are empty in this matched release declaration set.
struct ArenaAllocator13 { void* pool; void* begin; void* end; void* current; void* head; };
struct LinearAlloc13 { mutex_abi::Mutex lock; ArenaAllocator13 allocator; };
static_assert(offsetof(LinearAlloc13, allocator) == 40 && sizeof(ArenaAllocator13) == 40);
using ArenaContains = bool (*)(const void*, const void*);
ArenaContains arena_contains = nullptr;
bool Contains(const void* allocator, void* member) {
  return arena_contains(static_cast<const char*>(allocator) + offsetof(LinearAlloc13, allocator), member);
}
bool Fail(std::string* error, const char* message) { if(error)*error=message; return false; }
struct Frame {
  JNIEnv* env; bool valid;
  explicit Frame(JNIEnv* e):env(e),valid(e->PushLocalFrame(8)==JNI_OK){}
  ~Frame(){if(valid)env->PopLocalFrame(nullptr);}
};
bool Methods(jvmtiEnv* ti, jclass klass, std::vector<jmethodID>* result) {
  jint count=0; jmethodID* methods=nullptr;
  if(ti->GetClassMethods(klass,&count,&methods)!=JVMTI_ERROR_NONE)return false;
  if(count>0)result->assign(methods,methods+count);
  if(methods)ti->Deallocate(reinterpret_cast<unsigned char*>(methods));
  return !result->empty();
}
bool Fields(jvmtiEnv* ti, jclass klass, std::vector<jfieldID>* result) {
  jint count=0; jfieldID* fields=nullptr;
  if(ti->GetClassFields(klass,&count,&fields)!=JVMTI_ERROR_NONE)return false;
  if(count>0)result->assign(fields,fields+count);
  if(fields)ti->Deallocate(reinterpret_cast<unsigned char*>(fields));
  return !result->empty();
}
}
bool ConfigureAllocatorMembership(HookEngine& hooks,const DeviceProfile& profile,JNIEnv* env,
                                  jobject fixture,AllocatorContains* output,std::string* error,
                                  bool force_arena) {
  if(!output)return Fail(error,"Missing allocator membership output");
  *output=nullptr;
  if(!profile.Matched() || !env || !fixture)
    return Fail(error,"Allocator membership requires calibrated identity and rooted fixture");
  auto named=reinterpret_cast<AllocatorContains>(hooks.FindSymbol("libart.so",cas::kLinearAllocContainsUnsafe));
  if(named && !force_arena){*output=named;return true;}
  auto arena=reinterpret_cast<ArenaContains>(hooks.FindSymbol("libartbase.so",
      "_ZNK3art14ArenaAllocator8ContainsEPKv"));
  using GetAllocator=void*(*)(void*,void*);
  auto get=reinterpret_cast<GetAllocator>(hooks.FindSymbol("libart.so",
      "_ZN3art11ClassLinker26GetAllocatorForClassLoaderENS_6ObjPtrINS_6mirror11ClassLoaderEEE"));
  auto locked=reinterpret_cast<AllocatorContains>(hooks.FindSymbol("libart.so",
      "_ZNK3art11LinearAlloc8ContainsEPv"));
  if(!arena || !get)return Fail(error,"Missing named allocator membership interfaces");
  JavaVM* vm=nullptr; jvmtiEnv* ti=nullptr;
  if(env->GetJavaVM(&vm)!=JNI_OK || vm->GetEnv(reinterpret_cast<void**>(&ti),JVMTI_VERSION_1_2)!=JNI_OK)
    return Fail(error,"Allocator membership requires JVMTI");
  Frame frame(env);
  if(!frame.valid)return Fail(error,"Allocator membership local frame unavailable");
  jclass klass=env->GetObjectClass(fixture), boot=env->FindClass("java/lang/Object");
  jobject loader=nullptr;
  std::vector<jmethodID> owned,foreign;
  std::vector<jfieldID> fields;
  if(env->ExceptionCheck() || !klass || !boot || ti->GetClassLoader(klass,&loader)!=JVMTI_ERROR_NONE ||
      !loader || !Methods(ti,klass,&owned) || !Fields(ti,klass,&fields) || !Methods(ti,boot,&foreign))
    return Fail(error,"Allocator membership public class identity unavailable");
  NativeObjectAccess access(env);
  if(!access.Supported())return Fail(error,"Allocator membership native access unavailable");
  void* allocator=get(RuntimeClassLinker(),access.Decode(loader));
  if(!allocator)return Fail(error,"Owned class loader has no allocator");
  int stack_value=0;
  auto matches=[&](void* member,bool expected) {
    return member && arena(static_cast<char*>(allocator)+offsetof(LinearAlloc13,allocator),member)==expected &&
        (!named || named(allocator,member)==expected) && (!locked || locked(allocator,member)==expected);
  };
  for(auto id:owned)if(!matches(DecodeMethodId(id),true))
    return Fail(error,"Owned methods disagree with source allocator membership");
  for(auto id:fields)if(!matches(DecodeFieldId(id),true))
    return Fail(error,"Owned fields disagree with source allocator membership");
  for(auto id:foreign)if(!matches(DecodeMethodId(id),false))
    return Fail(error,"Bootstrap methods disagree with source allocator membership");
  if(!matches(&stack_value,false))return Fail(error,"Allocator incorrectly contains native stack storage");
  arena_contains=arena;
  *output=Contains;
  return true;
}
}
