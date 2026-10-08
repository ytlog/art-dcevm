#pragma once
#include "platform/common/contextual_field_id.h"
#include <algorithm>
#include <cstdlib>
#include <vector>
namespace artdcevm::contextual_field_id {
// Every JNI lifetime ends outside Runnable. Published aliases retain only weak
// namespaces in the contextual owner; unpublished proofs release their roots.
struct Stage {
 Stage(JNIEnv* e,retirement::RetiredFieldRegistry& r):env(e),registry(r){}
 Stage(const Stage&)=delete;Stage& operator=(const Stage&)=delete;
 JNIEnv* env; retirement::RetiredFieldRegistry& registry;
 std::vector<std::shared_ptr<Proof>> proofs;
 ~Stage(){Clear();}
 void Clear(){for(auto& p:proofs)Release(env,&p);proofs.clear();Prune(env,registry);}
 bool Add(jobject field,jfieldID id,std::string* error){auto p=Prepare(env,field,id,error);
  if(!p)return false;
  if(std::any_of(proofs.begin(),proofs.end(),[&](const auto& q){return NativeField(q)==NativeField(p);})){Release(env,&p);if(error)*error="Duplicate contextual field identity";return false;}
  proofs.push_back(std::move(p));return true;}
 bool Contains(void* field)const{return std::count_if(proofs.begin(),proofs.end(),[&](const auto& p){return NativeField(p)==field;})==1;}
 bool Covers(const std::vector<void*>& fields)const{
  if(fields.size()!=proofs.size())return false;
  for(size_t i=0;i<fields.size();++i)if(!Contains(fields[i])||std::find(fields.begin(),fields.begin()+i,fields[i])!=fields.begin()+i)return false;
  return true;
 }
 // Caller first proves a complete native table equals this set. This callback
 // runs only after final validation and performs no JNI work. Alias storage is
 // prepared before entry; the registry retains its native retirement contract.
 void Retire(void* field){auto p=std::find_if(proofs.begin(),proofs.end(),[&](const auto& q){return NativeField(q)==field;});
  if(p==proofs.end())std::abort();registry.Retire(field);PublishRetired(*p,registry);}
};
}
