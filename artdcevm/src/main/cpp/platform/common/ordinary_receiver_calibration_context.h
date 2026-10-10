#pragma once
#include "platform/common/redefinition_gate.h"
#include <cstdlib>
namespace artdcevm::ordinary_receiver_calibration {
template<class Store,class Mappings> bool StorageAvailable(const Store& store,const Mappings& maps)noexcept{
 return store.Empty()&&maps.empty();
}
// Ordinary producer owns this transient snapshot only after refusing all live
// source/graph contexts. No mapping address survives its episode.
template<class Container> class MappingLifetime {
 public:
 explicit MappingLifetime(Container& values):values_(values){}
 ~MappingLifetime(){values_.clear();}
 MappingLifetime(const MappingLifetime&)=delete;MappingLifetime&operator=(const MappingLifetime&)=delete;
 private:Container&values_;
};
// Borrows only the caller's JNI/JVMTI receivers. This scope creates no source
// Run, graph or stop permission: the actual process gate must already be owned.
template<class Owner> class Context {
 public:
 Context(Owner& owner,Owner*& current,JavaVM* vm,JNIEnv* env,jvmtiEnv* ti,
         JNIEnv*& source_env,jvmtiEnv*& source_ti,JNIEnv*& root_env,jvmtiEnv*& declaration_ti):
     owner_(owner),current_(current),source_env_(source_env),source_ti_(source_ti),
     root_env_(root_env),declaration_ti_(declaration_ti),previous_root_(root_env){
  if(!env||!ti||current||source_env||source_ti||declaration_ti||!redefinition_gate::Owned(vm))return;
  current=&owner;source_env=env;source_ti=ti;root_env=env;declaration_ti=ti;entered_=true;
 }
 ~Context(){if(!entered_)return;if(current_!=&owner_)std::abort();
  declaration_ti_=nullptr;source_ti_=nullptr;source_env_=nullptr;
  root_env_=previous_root_;current_=nullptr;
 }
 Context(const Context&)=delete;Context&operator=(const Context&)=delete;
 bool Entered()const noexcept{return entered_;}
 private:
 Owner& owner_;Owner*& current_;JNIEnv*& source_env_;jvmtiEnv*& source_ti_;
 JNIEnv*& root_env_;jvmtiEnv*& declaration_ti_;JNIEnv* previous_root_;bool entered_=false;
};
}
