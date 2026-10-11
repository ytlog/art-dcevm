#pragma once
#include "platform/common/bounded_managed_roots.h"
#include "platform/common/prepared_root_bindings.h"
#include <jvmti.h>
#include <string>
#include <vector>

namespace artdcevm::native_interface_dispatch_domain::detail {
// Separate read-only census: no method/field change witness is created for an
// erroneous Class. The actual Verifier owns this storage and its JNI roots.
class ErrorCensus final {
 public:
 struct Node {
  bounded_managed_roots::Slot root{},loader{},parent{};
  std::vector<bounded_managed_roots::Slot> interfaces;
  std::string descriptor;uint32_t flags=0;
  void*klass=nullptr;void*native_loader=nullptr;void*native_parent=nullptr;
  std::vector<void*> native_interfaces;
 };
 size_t Size()const noexcept{return nodes_.size();}
 const std::vector<Node>& Nodes()const noexcept{return nodes_;}
 bool Stage(const runnable_jni::Context&context,jvmtiEnv*ti,jclass klass,jint status,bounded_managed_roots::Store&roots,
            bool*existing,std::string*error){
  auto*env=context.env;
  if(!env||!context.self||!existing||frozen_||(status&(JVMTI_CLASS_STATUS_ERROR|JVMTI_CLASS_STATUS_PREPARED))!=
      (JVMTI_CLASS_STATUS_ERROR|JVMTI_CLASS_STATUS_PREPARED))return Fail(error,"Unprepared erroneous Class lacks read-only domain census");
  if(!Bind(roots))return Fail(error,"Erroneous Class root store generation changed");
  *existing=false;for(const auto&node:nodes_)if(roots.Same(env,node.root,klass)){*existing=true;return !env->ExceptionCheck();}
  Node node;if(!Describe(env,ti,klass,&node,error))return false;
  if(!roots.Stage(context,klass,&node.root,error))return false;
  jobject loader=nullptr;if(!Loader(ti,klass,&loader)||env->ExceptionCheck())return Fail(error,"Erroneous Class loader unavailable");
  if(loader){const bool ok=roots.Stage(context,loader,&node.loader,error);env->DeleteLocalRef(loader);if(!ok)return false;}
  auto parent=env->GetSuperclass(klass);if(env->ExceptionCheck())return false;
  if(parent){const bool ok=roots.Stage(context,parent,&node.parent,error);env->DeleteLocalRef(parent);if(!ok)return false;}
  jint count=0;jclass*interfaces=nullptr;
  if(ti->GetImplementedInterfaces(klass,&count,&interfaces)!=JVMTI_ERROR_NONE||count<0)return Fail(error,"Erroneous Class public interfaces unavailable");
  node.interfaces.resize(size_t(count));bool ok=true;
  for(jint i=0;i<count;++i){if(ok)ok=roots.Stage(context,interfaces[i],&node.interfaces[size_t(i)],error);env->DeleteLocalRef(interfaces[i]);}
  if(interfaces)ti->Deallocate(reinterpret_cast<unsigned char*>(interfaces));
  if(!ok||env->ExceptionCheck())return false;nodes_.push_back(std::move(node));return true;
 }
 void BeginCheck(){seen_.assign(nodes_.size(),false);}
 bool Check(JNIEnv*env,jvmtiEnv*ti,jclass klass,jint status,const bounded_managed_roots::Store&roots,std::string*error){
  if(!Matches(roots))return Fail(error,"Erroneous Class root store generation changed");
  if((status&(JVMTI_CLASS_STATUS_ERROR|JVMTI_CLASS_STATUS_PREPARED))!=(JVMTI_CLASS_STATUS_ERROR|JVMTI_CLASS_STATUS_PREPARED))return Fail(error,"Erroneous Class changed prepared status");
  size_t index=SIZE_MAX;for(size_t i=0;i<nodes_.size();++i)if(roots.Same(env,nodes_[i].root,klass)){if(index!=SIZE_MAX)return Fail(error,"Repeated erroneous Class root");index=i;}
  if(env->ExceptionCheck()||index==SIZE_MAX||index>=seen_.size()||seen_[index])return Fail(error,"Erroneous Class appeared or repeated after census");
  const auto&node=nodes_[index];Node actual;if(!Describe(env,ti,klass,&actual,error)||actual.descriptor!=node.descriptor||actual.flags!=node.flags)return Fail(error,"Erroneous Class public identity changed");
  jobject loader=nullptr;bool ok=Loader(ti,klass,&loader)&&!env->ExceptionCheck()&&roots.Same(env,node.loader,loader);if(loader)env->DeleteLocalRef(loader);if(!ok)return Fail(error,"Erroneous Class loader changed");
  auto parent=env->GetSuperclass(klass);ok=!env->ExceptionCheck()&&roots.Same(env,node.parent,parent);if(parent)env->DeleteLocalRef(parent);if(!ok)return Fail(error,"Erroneous Class parent changed");
  jint count=0;jclass*interfaces=nullptr;if(ti->GetImplementedInterfaces(klass,&count,&interfaces)!=JVMTI_ERROR_NONE||count<0)return false;
  ok=size_t(count)==node.interfaces.size();
  for(jint i=0;i<count;++i){if(ok)ok=roots.Same(env,node.interfaces[size_t(i)],interfaces[i]);env->DeleteLocalRef(interfaces[i]);}
  if(interfaces)ti->Deallocate(reinterpret_cast<unsigned char*>(interfaces));
  if(!ok||env->ExceptionCheck())return Fail(error,"Erroneous Class interfaces changed");seen_[index]=true;return true;
 }
 bool EndCheck()const noexcept{for(bool seen:seen_)if(!seen)return false;return seen_.size()==nodes_.size();}
 bool Freeze(const bounded_managed_roots::Store&roots,const prepared_roots::Registrar&registrar){
  if(frozen_||!EndCheck()||!Bind(roots))return false;
  for(auto&node:nodes_){if(!roots.Read(node.root,&node.klass)||!node.klass||!roots.Read(node.loader,&node.native_loader)||!roots.Read(node.parent,&node.native_parent))return false;
   node.native_interfaces.resize(node.interfaces.size());for(size_t i=0;i<node.interfaces.size();++i)if(!roots.Read(node.interfaces[i],&node.native_interfaces[i]))return false;
   for(auto*value:{&node.klass,&node.native_loader,&node.native_parent})if(!registered_.Add(value,registrar))return false;
   for(auto&value:node.native_interfaces)if(!registered_.Add(&value,registrar))return false;
  }
  registered_.Seal();frozen_=true;return true;
 }
 bool CanRefresh(const prepared_roots::Resolver&r)const noexcept{return frozen_&&store_&&Matches(*store_)&&store_->CanRefreshManagedRoots(r)&&registered_.CanRefresh(r);}
 void Refresh(const prepared_roots::Resolver&r)noexcept{if(CanRefresh(r))registered_.Refresh(r);}
 bool Fresh(const bounded_managed_roots::Store&roots)const noexcept{
  if(!frozen_||!Matches(roots)||!roots.RootsResolved())return false;for(const auto&node:nodes_){void*value=nullptr;
   if(!roots.Read(node.root,&value)||value!=node.klass||!roots.Read(node.loader,&value)||value!=node.native_loader||
      !roots.Read(node.parent,&value)||value!=node.native_parent)return false;
   for(size_t i=0;i<node.interfaces.size();++i)if(!roots.Read(node.interfaces[i],&value)||value!=node.native_interfaces[i])return false;
  }return true;
 }
 private:
 bool Matches(const bounded_managed_roots::Store&roots)const noexcept{return store_==&roots&&generation_==roots.Generation();}
 bool Bind(const bounded_managed_roots::Store&roots)noexcept{
  if(!store_){store_=&roots;generation_=roots.Generation();}
  return Matches(roots);
 }
 static bool Fail(std::string*error,const char*reason){if(error)*error=reason;return false;}
 static bool Loader(jvmtiEnv*ti,jclass klass,jobject*out){return ti->GetClassLoader(klass,out)==JVMTI_ERROR_NONE;}
 static bool Describe(JNIEnv*,jvmtiEnv*ti,jclass klass,Node*out,std::string*error){
  char*signature=nullptr;jint flags=0;
  if(ti->GetClassSignature(klass,&signature,nullptr)!=JVMTI_ERROR_NONE||!signature)return Fail(error,"Erroneous Class signature unavailable");
  out->descriptor=signature;ti->Deallocate(reinterpret_cast<unsigned char*>(signature));
  if(ti->GetClassModifiers(klass,&flags)!=JVMTI_ERROR_NONE)return Fail(error,"Erroneous Class modifiers unavailable");
  out->flags=uint32_t(flags);return true;
 }
 const bounded_managed_roots::Store*store_=nullptr;uint64_t generation_=0;
 std::vector<Node>nodes_;std::vector<bool>seen_;prepared_roots::Bindings registered_;bool frozen_=false;
};
}
