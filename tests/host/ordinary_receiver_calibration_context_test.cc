#include "platform/common/ordinary_receiver_calibration_context.h"
#include "platform/common/redefinition_gate.cc"
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace artdcevm;
namespace {
jvmtiError JNICALL Phase(jvmtiEnv*,jvmtiPhase* out){*out=JVMTI_PHASE_ONLOAD;return JVMTI_ERROR_NONE;}
jvmtiError Original(void*,void*,void*,const void*,int32_t,void*){return JVMTI_ERROR_NONE;}
struct Hooks final:HookEngine {
 bool Initialize(std::string*)override{return true;}
 void*FindSymbol(const char*,const char*name)override{assert(std::strstr(name,"RedefineClassesDirect"));return reinterpret_cast<void*>(Original);}
 void*Install(void*target,void*,void**original,std::string*)override{*original=target;return this;}
 bool Remove(void*,std::string*)override{return true;}
};
struct Owner{};
}
int main(){
 using ordinary_receiver_calibration::Context;
 Owner owner,foreign;Owner*current=nullptr;
 JavaVM vm{},other_vm{};JNIEnv env{},previous_root{},foreign_env{};
 jvmtiInterface_1 functions{};functions.GetPhase=Phase;jvmtiEnv ti{&functions},foreign_ti{&functions};
 JNIEnv*source_env=nullptr,*root_env=&previous_root;jvmtiEnv*source_ti=nullptr,*declared_ti=nullptr;
 Hooks hooks;std::string error;assert(redefinition_gate::InstallStartup(hooks,&vm,&ti,&error));
 auto restored=[&]{assert(!current&&!source_env&&!source_ti&&!declared_ti&&root_env==&previous_root);};
 {Context<Owner> denied{owner,current,&vm,&env,&ti,source_env,source_ti,root_env,declared_ti};assert(!denied.Entered());}restored();
 {redefinition_gate::Lease lease(&vm);assert(lease.Acquired());
  {Context<Owner> wrong_vm{owner,current,&other_vm,&env,&ti,source_env,source_ti,root_env,declared_ti};assert(!wrong_vm.Entered());}restored();
  current=&foreign;
  {Context<Owner> denied{owner,current,&vm,&env,&ti,source_env,source_ti,root_env,declared_ti};assert(!denied.Entered()&&current==&foreign);}current=nullptr;
  source_env=&foreign_env;
  {Context<Owner> denied{owner,current,&vm,&env,&ti,source_env,source_ti,root_env,declared_ti};assert(!denied.Entered()&&source_env==&foreign_env);}source_env=nullptr;
  source_ti=&foreign_ti;
  {Context<Owner> denied{owner,current,&vm,&env,&ti,source_env,source_ti,root_env,declared_ti};assert(!denied.Entered()&&source_ti==&foreign_ti);}source_ti=nullptr;
  declared_ti=&foreign_ti;
  {Context<Owner> denied{owner,current,&vm,&env,&ti,source_env,source_ti,root_env,declared_ti};assert(!denied.Entered()&&declared_ti==&foreign_ti);}declared_ti=nullptr;
  std::thread denied_thread([&]{Owner*local_current=nullptr;JNIEnv*local_env=nullptr,*local_root=&previous_root;jvmtiEnv*local_ti=nullptr,*local_decl=nullptr;
   Context<Owner> denied{owner,local_current,&vm,&env,&ti,local_env,local_ti,local_root,local_decl};assert(!denied.Entered()&&!local_current&&!local_env&&!local_ti&&!local_decl&&local_root==&previous_root);
  });denied_thread.join();
  for(int outcome=0;outcome<3;++outcome){std::vector<int> mappings;int cleanup=0;
   auto episode=[&]()->bool {try{
    Context<Owner> borrow{owner,current,&vm,&env,&ti,source_env,source_ti,root_env,declared_ti};assert(borrow.Entered());
    assert(current==&owner&&source_env==&env&&source_ti==&ti&&declared_ti==&ti&&root_env==&env);
    Context<Owner> recursive{owner,current,&vm,&env,&ti,source_env,source_ti,root_env,declared_ti};assert(!recursive.Entered());
    ordinary_receiver_calibration::MappingLifetime<std::vector<int>> snapshot{mappings};mappings.push_back(19);
    struct Cleanup {int&count;Owner*&current;Owner&owner;JavaVM*vm;~Cleanup(){assert(current==&owner&&redefinition_gate::Owned(vm));++count;}} own{cleanup,current,owner,&vm};
    if(outcome==2)throw std::runtime_error("private original failure");
    if(outcome==1)return false; // ordinary refusal unwinds the same owners.
    return true;
   }catch(const std::runtime_error&){return false;}};
   assert(episode()==(outcome==0));
   assert(cleanup==1&&mappings.empty());restored();
  }
  restored();assert(redefinition_gate::Owned(&vm));
 }
 restored();assert(!redefinition_gate::Owned(&vm));
 std::cout<<"Actual process gate + ordinary receiver context: foreign/recursive/thread/borrow refusal, root-env restoration and mapping cleanup on normal/refusal/exception PASS\n";
}
