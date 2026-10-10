#include "platform/android13/member_access_guards.h"
#include "platform/abi/arm64/android13/member_access_guards_abi.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/handle_root_scope.h"
#include "platform/abi/arm64/android13/handle_root_scope_abi.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/loaded_class_guards.h"
#include "platform/android13/retired_member_guard.h"
#include "platform/common/modern_final_field_guard.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include "platform/abi/arm64/android13/field_copy_metadata_abi.h"
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace artdcevm::android13 {
namespace {
using namespace member_access_abi;
using Class=validation_abi::ClassPrefix;
using Unary=void* (*)(void*);
using Interface=void* (*)(void*,uint32_t);
using Resolve=void* (*)(void*,uint32_t,void*,void*);
Unary root, obsolete_cache, obsolete_class, interface_types;
Interface direct_interface;
Resolve resolve_field;
uint32_t (*access_template)(uint32_t);
void (*throw_final)(void*,void*);
ThrowGuardException throw_exception;
std::atomic<bool> receiver_enabled{false};
std::atomic<bool> receiver_required{false};
template<class T> T Read(const void* object,size_t offset) {
  T value; std::memcpy(&value,static_cast<const unsigned char*>(object)+offset,sizeof(value)); return value;
}
void Require(bool yes) { if (!yes) std::abort(); }
uint32_t Flags(void* member) { return __atomic_load_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(member)+4),__ATOMIC_RELAXED); }
uint32_t ClassFlags(void* klass) { return Read<uint32_t>(klass,offsetof(Class,access_flags)); }
void* Declaring(void* member) { return root(member); }
void* Parent(void* klass) { return ReadHeapReference(klass,offsetof(Class,super_class)); }
void Throw(void* thread,const char* type,const char* text,void* result=nullptr) {
  if (result) std::memset(result,0,sizeof(uint64_t));
  throw_exception(thread,type,text);
}
bool Receiver(void* thread,jobject receiver,void* member,const char* message) {
  if (!ReceiverGuardEnabled() || !receiver || !member || (Flags(member)&kStaticFlag)) return false;
  void* object=DecodeRunnableObject(thread,receiver);
  if (!object || IsValidationAssignableFrom(Declaring(member),ReadHeapReference(object,0))) return false;
  Throw(thread,"Ljava/lang/IncompatibleClassChangeError;",message); return true;
}
void* AccessThread(const void* access) { return Read<void*>(access,offsetof(RunnableAccess,self)); }
uint32_t Operand(const void* operands) { return static_cast<const art::InstructionOperands*>(operands)->GetOperand(0); }
uint32_t DirectInterfaceCount(void* klass) {
  if ((Read<uint32_t>(klass,offsetof(Class,primitive_type))&0xffff)!=0) return 0;
  if (ReadHeapReference(klass,offsetof(Class,component_type))) return 2;
  if (ClassFlags(klass)&kProxyClass) {
    auto* fields=reinterpret_cast<void*>(Read<uint64_t>(klass,offsetof(Class,sfields)));
    Require(fields && Read<uint32_t>(fields,0)>0);
    auto* first=static_cast<unsigned char*>(fields)+field_copy_abi::kFieldArrayDataOffset;
    void* interfaces=ReadHeapReference(klass,Read<uint32_t>(first,offsetof(field_copy_abi::ArtField,offset)));
    return interfaces ? Read<uint32_t>(interfaces,kArrayLength) : 0;
  }
  void* list=interface_types(klass); return list ? Read<uint32_t>(list,0) : 0;
}
bool CanChange(void* field,void* method) {
  if (!(Flags(field)&kFinalFlag)) return true;
  void* declaring=Declaring(field), *referring=Declaring(method);
  if (declaring==referring) return true;
  if (!(ClassFlags(referring)&kObsoleteClass)) return false;
  for (void* current=declaring;current;current=obsolete_class(current)) if (current==referring) return true;
  return false;
}
}
void SetReceiverGuardEnabled(bool enabled) { receiver_enabled.store(enabled,std::memory_order_release); }
void SetReceiverGuardRequired(bool required) { receiver_required.store(required,std::memory_order_release); }
bool ReceiverGuardEnabled() { return receiver_enabled.load(std::memory_order_acquire); }
bool ReceiverGuardRequired() { return receiver_required.load(std::memory_order_acquire); }
bool GuardNativeFieldReceiver(JNIEnv* env,jobject object,jfieldID id) {
  NativeObjectAccess access(env); Require(access.Supported());
  return Receiver(access.Self(),object,DecodeFieldId(id),"JNI field receiver has lost its declaring class");
}
bool GuardRunnableFieldReceiver(const void* access,jobject object,jfieldID id) {
  return Receiver(AccessThread(access),object,DecodeFieldId(id),"JNI field receiver has lost its declaring class");
}
bool GuardRunnableMethodReceiver(const void* access,jobject object,void* method) {
  return Receiver(AccessThread(access),object,method,"JNI method receiver has lost its declaring class");
}
bool GuardRunnableMethodIdReceiver(const void* access,jobject object,jmethodID id) {
  return GuardRunnableMethodReceiver(access,object,DecodeMethodId(id));
}
bool GuardMethodHandleTarget(void* thread,void* slot,void* result) {
  // StackReference is a compressed GC-updated root, never a heap field.
  auto* handle=reinterpret_cast<void*>(static_cast<uintptr_t>(Read<uint32_t>(slot,0)));
  const auto kind=Read<uint32_t>(handle,offsetof(MethodHandle,kind));
  auto* target=reinterpret_cast<void*>(Read<uint64_t>(handle,offsetof(MethodHandle,target)));
  if (kind==kVirtual || kind==kDirect || kind==kStatic || kind==kInterface)
    return GuardRetiredMethod(target,thread,result);
  if (kind>=kInstanceGet && kind<=kStaticPut) return GuardRetiredField(target,thread,result);
  if (kind!=kSuper) return false;
  if (GuardRetiredMethod(target,thread,result)) return true;
  void* declaring=Declaring(target);
  void* type=ReadHeapReference(handle,offsetof(MethodHandle,method_type));
  void* parameters=ReadHeapReference(type,offsetof(MethodType,parameters));
  void* caller=Read<uint32_t>(parameters,kArrayLength)>0 ? ReadHeapReference(parameters,kArrayData) : nullptr;
  bool related=caller==declaring;
  const bool is_interface=(ClassFlags(declaring)&kInterfaceFlag)!=0;
  if (caller && !related) {
    if (is_interface) {
      for (uint32_t i=0,count=DirectInterfaceCount(caller);!related && i<count;++i) {
        void* direct=direct_interface(caller,i);
        related=direct && IsValidationAssignableFrom(declaring,direct);
      }
    } else {
      void* parent=Parent(caller); related=parent && IsValidationAssignableFrom(declaring,parent);
    }
  }
  if (related) return false;
  Throw(thread,"Ljava/lang/IncompatibleClassChangeError;",is_interface ?
      "Special caller has lost its direct interface relationship" :
      "Special caller has lost its superclass relationship",result);
  return true;
}
bool GuardFieldVarHandle(void* handle,int mode,void* frame,const void* operands,void* result) {
  auto* field=reinterpret_cast<void*>(Read<uint64_t>(handle,offsetof(FieldVarHandle,field)));
  void* self=CurrentHierarchyThread();
  if (GuardRetiredField(field,self,result)) return true;
  if ((Flags(field)&kFinalFlag) && mode>=0 && mode<=kLastAccessMode && access_template(mode)!=0) {
    Throw(self,"Ljava/lang/UnsupportedOperationException;","Final field does not support VarHandle write access",result);
    return true;
  }
  if (!ReceiverGuardEnabled() || (Flags(field)&kStaticFlag)) return false;
  uint32_t index=Operand(operands);
  const uint32_t count=Read<uint32_t>(frame,offsetof(ShadowFrame,registers));
  Require(index<count);
  void* receiver=reinterpret_cast<void*>(static_cast<uintptr_t>(Read<uint32_t>(frame,sizeof(ShadowFrame)+size_t{count}*4+size_t{index}*4)));
  if (!receiver || IsValidationAssignableFrom(Declaring(field),ReadHeapReference(receiver,0))) return false;
  // Preserve the original receiver rejection: its result is not cleared here.
  Throw(self,"Ljava/lang/IncompatibleClassChangeError;","VarHandle receiver has lost its field declaring class");
  return true;
}
bool GuardMethodHandleFieldPut(void* field) {
  void* self=CurrentHierarchyThread();
  if (GuardRetiredField(field,self,nullptr)) return true;
  if (!final_field_guard::FinalFieldGuardEnabled() || !(Flags(field)&kFinalFlag)) return false;
  Throw(self,"Ljava/lang/IllegalAccessError;","MethodHandle setter targets a final field"); return true;
}
bool GuardNterpFinalFieldPut(void* self,void* method,uint16_t* pc) {
  if (!final_field_guard::FinalFieldGuardEnabled()) return false;
  const uint8_t op=pc[0]&0xff;
  if (!((op>=0x59 && op<=0x5f) || (op>=0x67 && op<=0x6d))) return false;
  HandleRootScope roots(self,2);
  auto* declaring=Declaring(method);
  roots.Set(0,(Flags(method)&kObsoleteMethod) ? obsolete_cache(method) :
      ReadHeapReference(declaring,offsetof(Class,dex_cache)));
  // Reacquire the class after GetObsoleteDexCache consults runtime metadata.
  roots.Set(1,ReadHeapReference(Declaring(method),offsetof(Class,class_loader)));
  auto* field=resolve_field(RuntimeClassLinker(),pc[1],roots.Slot(0),roots.Slot(1));
  if (!field) {
    // Fixed Thread::exception_ is read by HandleRootScope's calibrated profile.
    return Read<void*>(self,handle_root_abi::kExceptionOffset)!=nullptr;
  }
  if ((Flags(field)&kFinalFlag) && !CanChange(field,method)) { throw_final(method,field); return true; }
  return false;
}
bool ConfigureMemberAccessGuards(HookEngine& hooks,const DeviceProfile& profile,std::string* error) {
  if(!profile.Matched()){*error="Member access guards require a matched device profile";return false;}
  root=reinterpret_cast<Unary>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
  obsolete_cache=reinterpret_cast<Unary>(hooks.FindSymbol("libart.so","_ZN3art9ArtMethod19GetObsoleteDexCacheEv"));
  obsolete_class=reinterpret_cast<Unary>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Class16GetObsoleteClassEv"));
  interface_types=reinterpret_cast<Unary>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Class20GetInterfaceTypeListEv"));
  direct_interface=reinterpret_cast<Interface>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Class18GetDirectInterfaceEj"));
  access_template=reinterpret_cast<decltype(access_template)>(hooks.FindSymbol("libart.so","_ZN3art6mirror9VarHandle21GetAccessModeTemplateENS1_10AccessModeE"));
  resolve_field=reinterpret_cast<Resolve>(hooks.FindSymbol("libart.so","_ZN3art11ClassLinker15ResolveFieldJLSEjNS_6HandleINS_6mirror8DexCacheEEENS1_INS2_11ClassLoaderEEE"));
  throw_final=reinterpret_cast<decltype(throw_final)>(hooks.FindSymbol("libart.so","_ZN3art33ThrowIllegalAccessErrorFinalFieldEPNS_9ArtMethodEPNS_8ArtFieldE"));
  throw_exception=reinterpret_cast<ThrowGuardException>(hooks.FindSymbol("libart.so",kGuardThrowNewException));
  if (!root || !obsolete_cache || !obsolete_class || !interface_types || !direct_interface ||
      !access_template || !resolve_field || !throw_final || !throw_exception) {
    *error="Missing named ART member access guard"; return false;
  }
  return true;
}
}
