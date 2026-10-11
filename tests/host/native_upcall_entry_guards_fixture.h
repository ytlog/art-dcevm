#pragma once
// Real production entry owners installed through named host ART boundaries.
// This fixture never supplies MethodEntryGuardsOwn/FieldEntryGuardsOwn doubles.
#include "platform/common/modern_method_guards.h"
#include "platform/common/modern_field_guards.h"
#include "platform/common/native_exception_entry_ownership.h"
#include "platform/common/var_handle_access_mode.h"
#include "platform/abi/common/suspend_all.h"
#include <cassert>
#include <array>
#include <map>
#include <utility>
#include <set>

namespace upcall_entry_fixture {
using namespace artdcevm;
struct Boundaries {
  void* (*current)()=nullptr;
  void (*raise)(void*,const char*,const char*)=nullptr;
  void* (*decode_method)(void*,jmethodID)=nullptr;
  abi::common::FromReflectedMethod reflected_method=nullptr;
  field_mirror::NamedField existing_field=nullptr;
  abi::common::SuspendConstruct suspend=nullptr;
  abi::common::SuspendDestroy resume=nullptr;
  bool (*exclusive)()=nullptr;
  void* (*allocate_mirror)(size_t)=nullptr;
};
struct MirrorRecord {abi::arm64::android12::ReflectedField* mirror;void* native;};
inline Boundaries boundaries;
inline std::vector<MirrorRecord> mirrors;
inline void* FieldLeaf(void* mirror) {
  assert(boundaries.current&&boundaries.current()&&!boundaries.exclusive());
  for(const auto& entry:mirrors)if(entry.mirror==mirror)return entry.native;
  return boundaries.existing_field?boundaries.existing_field(mirror):nullptr;
}
// Every named host entry has a distinct compiled address. Its typed ART ABI
// remains outside this ownership-only fixture; invoking any such leaf aborts.
inline volatile size_t invoked_unknown_entry=SIZE_MAX;
template<size_t Index> inline void OriginalEntry() {
  invoked_unknown_entry=Index;std::abort();
}
template<size_t... Index> inline auto OriginalEntries(std::index_sequence<Index...>) {
  return std::array<void(*)(),sizeof...(Index)>{OriginalEntry<Index>...};
}
inline const auto original_entries=OriginalEntries(std::make_index_sequence<136>{});
class Hooks final:public HookEngine {
 public:
  explicit Hooks(HookEngine& base,Boundaries leaves):base_(base){
    assert(mirrors.empty());boundaries=leaves;
    assert(leaves.current&&leaves.raise&&leaves.decode_method&&leaves.reflected_method&&
           leaves.suspend&&leaves.resume&&leaves.exclusive&&leaves.allocate_mirror);
  }
  bool Initialize(std::string* error)override{return base_.Initialize(error);}
  void* FindSymbol(const char* library,const char* name)override {
    const std::string symbol(name);
    if(symbol==abi::common::kSuspendConstruct)return reinterpret_cast<void*>(boundaries.suspend);
    if(symbol==abi::common::kSuspendDestroy)return reinterpret_cast<void*>(boundaries.resume);
    if(symbol=="_ZN3art6Thread14CurrentFromGdbEv")return reinterpret_cast<void*>(boundaries.current);
    if(symbol=="_ZN3art6Thread17ThrowNewExceptionEPKcS2_")return reinterpret_cast<void*>(boundaries.raise);
    if(symbol=="_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID")return reinterpret_cast<void*>(boundaries.decode_method);
    if(symbol==abi::common::kFromReflectedMethod)return reinterpret_cast<void*>(boundaries.reflected_method);
    if(symbol=="_ZN3art6mirror5Field11GetArtFieldEv")return reinterpret_cast<void*>(FieldLeaf);
    if(symbol==var_handle_access_mode::kNamedTemplate)return reinterpret_cast<void*>(var_handle_access_mode::Template);
    // Named declaration membership, rather than a permissive unknown-symbol fallback.
    const bool method=symbol.find("CheckMethodAndSig")!=std::string::npos||
      symbol.find("ArtMethod6Invoke")!=std::string::npos||symbol.find("InvokeMethod")!=std::string::npos||
      symbol.find("InvokeWithJValues")!=std::string::npos||symbol.find("InvokeWithVarArgs")!=std::string::npos||
      symbol.find("InvokeVirtualOrInterfaceWith")!=std::string::npos;
    const bool field=symbol=="_ZN3art6mirror5Field11VisitTargetEPNS_22ReflectiveValueVisitorE"||
      ((symbol.find("_ZN3art3JNIILb")==0||symbol.find("_ZN3art12_GLOBAL__N_18CheckJNI")==0||symbol.find("_ZN3artL")==0)&&
       (symbol.find("Field")!=std::string::npos));
    if(method||field){
      named_entries.insert(symbol);
      auto found=named_targets.find(symbol);
      if(found==named_targets.end()) {
        assert(named_targets.size()<original_entries.size());
        void* target=reinterpret_cast<void*>(original_entries[named_targets.size()]);
        found=named_targets.emplace(symbol,target).first;
      }
      return found->second;
    }
    return base_.FindSymbol(library,name);
  }
  void* Install(void* target,void* proxy,void** original,std::string*)override {
    assert(boundaries.exclusive()&&target&&proxy&&original);
    bool named=target==reinterpret_cast<void*>(FieldLeaf);
    for(const auto& entry:named_targets)named|=entry.second==target;
    assert(named&&installed_targets.count(target)==0&&installed_proxies.count(proxy)==0);
    *original=target;installed_targets.emplace(target,proxy);installed_proxies.insert(proxy);return proxy;
  }
  bool Remove(void* token,std::string*)override {
    if(installed_proxies.erase(token)!=1)return false;
    for(auto it=installed_targets.begin();it!=installed_targets.end();++it)
      if(it->second==token){installed_targets.erase(it);return true;}
    std::abort();
  }
  std::set<std::string> named_entries;
  std::set<void*> installed_proxies;
  std::map<std::string,void*> named_targets;
  std::map<void*,void*> installed_targets;
 private:HookEngine& base_;
};

inline std::unique_ptr<native_exception_entry_ownership::Service> Prepare(
    Hooks& hooks,JNIEnv* env,jobject strong_class,void* manager,
    const modern_field_metadata::FieldMetadata& metadata,
    retirement::RetiredMethodRegistry& methods,retirement::RetiredFieldRegistry& fields,
    std::string* error) {
  // Empty and foreign ownership cannot fabricate a closed Proof.
  assert(!native_exception_entry_ownership::Service::Prepare(methods,fields,manager));
  field_mirror::Calibration calibration;
  struct Request {
    const modern_field_metadata::FieldMetadata* metadata;jobject root;
    field_mirror::Calibration* calibration;std::string* error;
  } request{&metadata,strong_class,&calibration,error};
  const auto calibrate=[](const runnable_jni::Context& context,void* raw){
    auto& r=*static_cast<Request*>(raw);void* klass=nullptr;
    if(!runnable_executor::DecodeStrongGlobal(context,r.root,&klass))return false;
    std::vector<modern_field_metadata::FieldOracle> oracles;
    for(bool is_static:{false,true}) {
      field_array::View view;if(!r.metadata->Fields(klass,is_static,&view,r.error))return false;
      for(uint32_t index=0;index<view.count;++index)if(view.Selected(index)) {
        auto* native=static_cast<field_storage::Field*>(view.At(index));
        auto* mirror=new(boundaries.allocate_mirror(sizeof(abi::arm64::android12::ReflectedField)))abi::arm64::android12::ReflectedField{};
        mirror->declaring_class=uint32_t(uintptr_t(klass));mirror->access_flags=native->access_flags;
        mirror->art_field_index=index;mirror->offset=native->offset;
        mirrors.push_back({mirror,native});
        // Existing executor consumes owned strong roots; host allocator mirrors
        // use the same real public NewGlobalRef boundary as the surrounding fixture.
        jobject root=context.env->NewGlobalRef(reinterpret_cast<jobject>(mirror));
        if(!root||context.env->ExceptionCheck())return false;
        oracles.push_back({root,"unused-name","I",native->access_flags});
      }
    }
    const bool passed=field_mirror::Calibrate(context,*r.metadata,FieldLeaf,r.root,
      oracles.data(),oracles.size(),r.calibration,r.error);
    for(const auto& oracle:oracles)context.env->DeleteGlobalRef(oracle.strong_global_field);
    return passed;
  };
  if(runnable_executor::With(env,strong_class,calibrate,&request)!=runnable_jni::Result::kSuccess)return nullptr;
  if(!retirement::InstallMethodEntryGuards(hooks,env,strong_class,manager,methods,error)||
     !retirement::InstallFieldEntryGuards(hooks,env,strong_class,manager,calibration,fields,error))return nullptr;
  assert(hooks.installed_proxies.size()==136&&hooks.installed_targets.size()==136); // 8 method + 2 Field leaves + 9 * 14 accessors.
  auto proof=native_exception_entry_ownership::Service::Prepare(methods,fields,manager);assert(proof);
  retirement::RetiredMethodRegistry foreign_methods;retirement::RetiredFieldRegistry foreign_fields;
  assert(!native_exception_entry_ownership::Service::Prepare(foreign_methods,fields,manager));
  assert(!native_exception_entry_ownership::Service::Prepare(methods,foreign_fields,manager));
  int foreign_manager;
  assert(!native_exception_entry_ownership::Service::Prepare(methods,fields,&foreign_manager));
  assert(!native_exception_entry_ownership::Service::Prepare(methods,fields,nullptr));
  return proof;
}
} // namespace upcall_entry_fixture
