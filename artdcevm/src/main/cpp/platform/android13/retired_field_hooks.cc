#include "platform/android13/retired_field_hooks.h"
#include "platform/android13/member_decode.h"
#include "platform/android13/reflected_field_visit.h"
#include "platform/android13/member_access_guards.h"
#include "platform/android13/symbols/profile.h"
#include <jni.h>
#include <vector>
#include <type_traits>
#include <atomic>

namespace artdcevm::android13 {
namespace {
std::atomic<bool> field_access_checks{false};
// AOSP13 entrypoints/entrypoint_utils.h FindFieldFlags, fixed source values.
// All callees below have ArtField*(uint32_t,ArtMethod*,Thread*,size_t).
using CheckedField=void*(*)(uint32_t,void*,void*,size_t);
constexpr int kInstanceBit=1<<0,kStaticBit=1<<1,kObjectBit=1<<2,
    kPrimitiveBit=1<<3,kReadBit=1<<4,kWriteBit=1<<5;
constexpr int kFieldTypes[]={
 kInstanceBit|kObjectBit|kReadBit,kInstanceBit|kObjectBit|kWriteBit,
 kInstanceBit|kPrimitiveBit|kReadBit,kInstanceBit|kPrimitiveBit|kWriteBit,
 kStaticBit|kObjectBit|kReadBit,kStaticBit|kObjectBit|kWriteBit,
 kStaticBit|kPrimitiveBit|kReadBit,kStaticBit|kPrimitiveBit|kWriteBit};
static_assert(kFieldTypes[0]==21&&kFieldTypes[1]==37&&kFieldTypes[2]==25&&kFieldTypes[3]==41&&
              kFieldTypes[4]==22&&kFieldTypes[5]==38&&kFieldTypes[6]==26&&kFieldTypes[7]==42);
CheckedField checked_fields[8]{};
template<int type> struct BytecodeField {
 static inline void* original=nullptr;
 static inline CheckedField checked=nullptr;
 static void* Resolve(uint32_t index,void* caller,void* self,size_t size){
  const auto resolve=FieldAccessChecksRequired()?checked:reinterpret_cast<CheckedField>(original);
  return resolve(index,caller,self,size);
 }
};
bool GuardNterpFieldAccess(void* self,void* caller,uint16_t* pc){
 if(!FieldAccessChecksRequired())return false;
 const unsigned op=pc[0]&0xffu;if(op<0x52||op>0x6d)return false;
 const unsigned group=(op-0x52)/7,kind=(op-0x52)%7;
 const bool statik=group>=2,write=(group&1)!=0,object=kind==2;
 constexpr size_t sizes[]={4,8,4,1,1,2,2};
 const unsigned family=(statik?4u:0u)+(object?0u:2u)+(write?1u:0u);
 // The unhooked checked family owns class/field access, static/type/final
 // checks, pending exceptions and initialization. A rejection must return
 // before the original Nterp resolver publishes an offset/field cache entry.
 return checked_fields[family](pc[1],caller,self,sizes[kind])==nullptr;
}
std::string FieldResolverSymbol(int type,bool checked){
 return "_ZN3art17FindFieldFromCodeILNS_13FindFieldTypeE"+std::to_string(type)+"ELb"+(checked?"1":"0")+"EEEPNS_8ArtFieldEjPNS_9ArtMethodEPNS_6ThreadEm";
}
void* original_field;
void* original_visit;
void* original_varhandle;
void* original_method_handle_put;
void* original_check;
void* original_nterp_instance;
void* original_nterp_static;
struct Entry { std::string symbol; void* proxy; void** original; void* target = nullptr; };

void* GetField(void* field) { return DecodeReflectedField(field); }
void VisitField(void* field, void* visitor) { VisitReflectedFieldTarget(field, visitor); }
bool VarHandle(void* handle, int mode, void* frame, const void* operands, void* result) {
  if (GuardFieldVarHandle(handle, mode, frame, operands, result)) return false;
  using Fn = bool (*)(void*, int, void*, const void*, void*);
  return reinterpret_cast<Fn>(original_varhandle)(handle, mode, frame, operands, result);
}
bool MethodHandleFieldPut(void* thread, void* frame, void* receiver, void* field,
                          int primitive, void* value) {
  if (GuardMethodHandleFieldPut(field)) return false;
  using Fn = bool (*)(void*, void*, void*, void*, int, void*);
  return reinterpret_cast<Fn>(original_method_handle_put)(
      thread, frame, receiver, field, primitive, value);
}
bool CheckField(void* check, void* access, jobject receiver, jfieldID field,
                bool is_static, int primitive) {
  if (GuardRunnableFieldId(field)) return false;
  if (!is_static && GuardRunnableFieldReceiver(access, receiver, field)) return false;
  using Fn = bool (*)(void*, void*, jobject, jfieldID, bool, int);
  return reinterpret_cast<Fn>(original_check)(check, access, receiver, field, is_static, primitive);
}
uint32_t NterpInstanceField(void* thread, void* caller, uint16_t* dex_pc, size_t resolve_type) {
  if (GuardNterpFieldAccess(thread,caller,dex_pc) || GuardNterpFinalFieldPut(thread, caller, dex_pc)) return 0;
  using Fn = uint32_t (*)(void*, void*, uint16_t*, size_t);
  return reinterpret_cast<Fn>(original_nterp_instance)(thread, caller, dex_pc, resolve_type);
}
size_t NterpStaticField(void* thread, void* caller, uint16_t* dex_pc, size_t resolve_type) {
  if (GuardNterpFieldAccess(thread,caller,dex_pc) || GuardNterpFinalFieldPut(thread, caller, dex_pc)) return 0;
  using Fn = size_t (*)(void*, void*, uint16_t*, size_t);
  return reinterpret_cast<Fn>(original_nterp_static)(thread, caller, dex_pc, resolve_type);
}

// Typed callbacks preserve the AArch64 floating-point and integer return ABIs.
template <int indexed, typename T, bool is_static> struct JniField {
  using Receiver = std::conditional_t<is_static, jclass, jobject>;
  static inline void* original_get;
  static inline void* original_set;
  static T Get(JNIEnv* env, Receiver object, jfieldID field) {
    if (GuardNativeFieldId(env, field)) return T{};
    if constexpr (!is_static) {
      if (GuardNativeFieldReceiver(env, object, field)) return T{};
    }
    using Fn = T (*)(JNIEnv*, Receiver, jfieldID);
    return reinterpret_cast<Fn>(original_get)(env, object, field);
  }
  static void Set(JNIEnv* env, Receiver object, jfieldID field, T value) {
    if (GuardNativeFieldId(env, field)) return;
    if constexpr (!is_static) {
      if (GuardNativeFieldReceiver(env, object, field)) return;
    }
    using Fn = void (*)(JNIEnv*, Receiver, jfieldID, T);
    reinterpret_cast<Fn>(original_set)(env, object, field, value);
  }
  static void Add(std::vector<Entry>& entries, const char* type, const char* code) {
    entries.push_back({cas::JniFieldSymbol(indexed, is_static, false, type, code,
                                          std::is_same_v<T, jobject>),
                       reinterpret_cast<void*>(Get), &original_get});
    entries.push_back({cas::JniFieldSymbol(indexed, is_static, true, type, code,
                                          std::is_same_v<T, jobject>),
                       reinterpret_cast<void*>(Set), &original_set});
  }
};
template <typename T> struct ReflectionField {
  static inline void* original_get;
  static inline void* original_set;
  static T Get(JNIEnv* env, jobject field, jobject object) {
    if (GuardNativeReflectedField(env, field)) return T{};
    using Fn = T (*)(JNIEnv*, jobject, jobject);
    return reinterpret_cast<Fn>(original_get)(env, field, object);
  }
  static void Set(JNIEnv* env, jobject field, jobject object, T value) {
    if (GuardNativeReflectedField(env, field)) return;
    using Fn = void (*)(JNIEnv*, jobject, jobject, T);
    reinterpret_cast<Fn>(original_set)(env, field, object, value);
  }
  static void Add(std::vector<Entry>& entries, const char* type, const char* code) {
    entries.push_back({cas::ReflectionFieldSymbol(false, type, code,
                                                  std::is_same_v<T, jobject>),
                       reinterpret_cast<void*>(Get), &original_get});
    entries.push_back({cas::ReflectionFieldSymbol(true, type, code,
                                                  std::is_same_v<T, jobject>),
                       reinterpret_cast<void*>(Set), &original_set});
  }
};
}
void RequireFieldAccessChecks(){field_access_checks.store(true,std::memory_order_release);}
bool FieldAccessChecksRequired(){return field_access_checks.load(std::memory_order_acquire);}
bool InstallRetiredFieldHooks(HookEngine& hooks, const DeviceProfile& profile, std::string* error) {
  if (!ConfigureReflectedFieldVisit(hooks,profile,error)) return false;
  std::vector<Entry> entries{
    {cas::kRetiredFieldHooks[0], reinterpret_cast<void*>(GetField), &original_field},
    {cas::kRetiredFieldHooks[1], reinterpret_cast<void*>(VisitField), &original_visit},
    {cas::kRetiredFieldHooks[2], reinterpret_cast<void*>(VarHandle), &original_varhandle},
    {cas::kRetiredFieldHooks[3], reinterpret_cast<void*>(MethodHandleFieldPut), &original_method_handle_put},
    {cas::kRetiredFieldHooks[4], reinterpret_cast<void*>(CheckField), &original_check},
    {cas::kRetiredFieldHooks[5], reinterpret_cast<void*>(NterpInstanceField), &original_nterp_instance},
    {cas::kRetiredFieldHooks[6], reinterpret_cast<void*>(NterpStaticField), &original_nterp_static},
  };
#define ADD(TYPE, JNI_TYPE, CODE) \
  JniField<0, JNI_TYPE, false>::Add(entries, #TYPE, CODE); \
  JniField<1, JNI_TYPE, false>::Add(entries, #TYPE, CODE); \
  JniField<0, JNI_TYPE, true>::Add(entries, #TYPE, CODE); \
  JniField<1, JNI_TYPE, true>::Add(entries, #TYPE, CODE); \
  ReflectionField<JNI_TYPE>::Add(entries, #TYPE, CODE)
  ADD(Boolean, jboolean, "h"); ADD(Byte, jbyte, "a"); ADD(Char, jchar, "t");
  ADD(Short, jshort, "s"); ADD(Int, jint, "i"); ADD(Long, jlong, "l");
  ADD(Float, jfloat, "f"); ADD(Double, jdouble, "d"); ADD(Object, jobject, "");
#undef ADD
  // Resolve every original checked family before installing its unchecked
  // counterpart. Checked entry points themselves are never hooked here.
  for(size_t i=0;i<8;++i){checked_fields[i]=reinterpret_cast<CheckedField>(hooks.FindSymbol("libart.so",FieldResolverSymbol(kFieldTypes[i],true).c_str()));
    if(!checked_fields[i]){*error="Missing checked field resolver: "+FieldResolverSymbol(kFieldTypes[i],true);return false;}}
#define ADD_BYTECODE(TYPE, INDEX) \
  BytecodeField<TYPE>::checked=checked_fields[INDEX]; \
  entries.push_back({FieldResolverSymbol(TYPE,false),reinterpret_cast<void*>(BytecodeField<TYPE>::Resolve),&BytecodeField<TYPE>::original})
  ADD_BYTECODE(21,0);ADD_BYTECODE(37,1);ADD_BYTECODE(25,2);ADD_BYTECODE(41,3);
  ADD_BYTECODE(22,4);ADD_BYTECODE(38,5);ADD_BYTECODE(26,6);ADD_BYTECODE(42,7);
#undef ADD_BYTECODE
  for (auto& entry : entries) {
    entry.target = hooks.FindSymbol(symbols::LibraryName(symbols::Library::kArt), entry.symbol.c_str());
    if (!entry.target) { *error = "Missing field symbol: " + entry.symbol; return false; }
  }
  std::vector<void*> tokens;
  for (const auto& entry : entries) {
    void* token = hooks.Install(entry.target, entry.proxy, entry.original, error);
    if (!token) {
      for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        std::string rollback;
        if (!hooks.Remove(*it, &rollback)) *error += "; unhook failed: " + rollback;
      }
      return false;
    }
    tokens.push_back(token);
  }
  return true;
}
}
