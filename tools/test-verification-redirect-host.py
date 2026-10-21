#!/usr/bin/env python3
"""Exercise production verifier redirection with fake ART caches and rooted mappings."""
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include <iostream>
#include <stdexcept>
#include <map>
#include <set>
#include <sys/mman.h>
#include "platform/common/verification_redirect.cc"
using namespace artdcevm;
using namespace artdcevm::verification_redirect;
using abi::arm64::Handle;
using abi::arm64::RedefinitionDataIter;
using abi::arm64::ClassRedefinitionPrefix;
transaction::Context context;
bool active=true, ready=true, access_ok=true, result=true, modern=false;
int checks=0, raw_array_lookups=0, current_reads=0;
uint32_t access=1;
bool loader_ok=true, move_component=false;
void* loader_value=nullptr;
uint32_t* replacement_root=nullptr;
int loader_reads=0;
void* old_type=reinterpret_cast<void*>(0x100);
void* new_type=reinterpret_cast<void*>(0x200);
void* other_type=reinterpret_cast<void*>(0x300);
std::map<void*,void*> mapping;
std::set<std::string> missing;
void Need(bool condition,const char* why) { ++checks; if(!condition)throw std::runtime_error(why); }
namespace artdcevm::transaction {
Context* Current() noexcept { ++current_reads;return active ? &context : nullptr; }
const Requirements* Context::Find(ClassRedefinitionPrefix* self) const {
  auto it=targets.find(self); return it==targets.end()?nullptr:&it->second;
}
}
namespace artdcevm::runtime_thread {
std::optional<VerifiedThread> Ready() noexcept {
  return ready ? std::optional<VerifiedThread>(VerifiedThread(nullptr)) : std::nullopt;
}
void* Read(const VerifiedThread&, uint32_t value) noexcept { return reinterpret_cast<void*>(uintptr_t(value)); }
void* Current(const VerifiedThread& thread) noexcept { (void)thread.state_; return nullptr; }
const abi::arm64::RuntimeThread& Layout(const VerifiedThread&) noexcept {
  static abi::arm64::RuntimeThread layout{}; return layout;
}
}
namespace artdcevm::class_layout {
bool ClassLoader(const runtime_thread::VerifiedThread&, const void* klass, void** loader) {
  ++loader_reads;
  Need(klass==reinterpret_cast<void*>(uintptr_t(*replacement_root)),"loader lookup retained an unrooted Class");
  *loader=loader_value;return loader_ok;
}
void* Rebuilt(const runtime_thread::VerifiedThread&,const abi::arm64::RedefinitionDataHolderPrefix*,
              const void* old,const std::vector<int32_t>&) {
  auto it=mapping.find(const_cast<void*>(old));return it==mapping.end()?nullptr:it->second;
}
bool ClassAccess(const runtime_thread::VerifiedThread&,const void* klass,uint32_t* out) {
  Need(klass==new_type,"access probe did not use the replacement");*out=access;return access_ok;
}
}
namespace artdcevm::invoke_kind { void ObserveClassLinker(void*) {} }
extern "C" int __android_log_write(int,const char*,const char*) { return 0; }
extern "C" void* shadowhook_dlopen(const char*) { return reinterpret_cast<void*>(1); }
extern "C" void shadowhook_dlclose(void*) {}
extern "C" void* shadowhook_dlsym(void*,const char* name) {
  return missing.count(name)?nullptr:reinterpret_cast<void*>(1);
}
extern "C" void* shadowhook_dlsym_dynsym(void* h,const char* n) { return shadowhook_dlsym(h,n); }

void* received=nullptr;bool received_precision=false;
const void* CacheFind(const void*,void* klass,bool precise) {
  received=klass;received_precision=precise;return klass;
}
const void* CacheInsert(void*,const std::string_view& descriptor,void* klass,bool precise) {
  Need(descriptor=="Ltarget;","component descriptor changed");
  return CacheFind(nullptr,klass,precise);
}
const void* CacheFrom(void*,void* klass) { return CacheFind(nullptr,klass,false); }
void* Lookup(void*,void*,const char* descriptor,Handle) {
  if(descriptor[0]=='[') {
    ++raw_array_lookups;
    Need(!CurrentState()->verifying,"array creation retained replacement redirection");
    Need(RegisterClass(old_type)==old_type,"array allocation would publish a replacement component");
    Need(VerifiedClass(nullptr,descriptor+1,{})==nullptr,"nested component lookup escaped the published graph");
  }
  return other_type;
}
void* LookupLength(void* l,void* t,const char* d,size_t n,Handle h) {
  Need(std::string_view(d).size()==n,"descriptor length changed");return Lookup(l,t,d,h);
}
bool Verify(ClassRedefinitionPrefix*,const RedefinitionDataIter&) {
  Need(CurrentState()->verifying!=nullptr,"verification scope was not installed");
  if(modern) RegisterFromClass(nullptr,old_type);
  else RegisterInsertClass(nullptr,"Ltarget;",old_type,false);
  return result;
}
std::map<const void*,void*> components;
bool array_answer=false, leaf_answer=true;
int component_calls=0, constructor_calls=0;
const void* from_result=nullptr;
bool resolve_from=true;
const void* FromLegacy(void*,void*,const char* descriptor,bool) {
  if(resolve_from)FindClass(nullptr,nullptr,descriptor,{});
  return from_result;
}
const void* FromModern(void* cache,const char* descriptor) { return FromLegacy(cache,nullptr,descriptor,false); }
void ConstructLegacy(void*,void* linker,bool load,void* allocator,bool suspend) {
  Need(linker==old_type&&allocator==other_type&&load&&!suspend,"legacy constructor arguments changed");++constructor_calls;
}
void ConstructModern(void*,void* thread,void* linker,void* pool,Handle loader,const void* dex,bool load,bool suspend) {
  Need(thread==new_type&&linker==old_type&&pool==other_type&&!loader.reference&&dex==old_type&&load&&!suspend,
       "modern constructor arguments changed");++constructor_calls;
}
void* verifier_token=reinterpret_cast<void*>(0x700);
const void* LegacyComponent(void* cache,const void* array,void* loader) {
  Need(loader==loader_value,"component lookup retained a loader across suspension");
  if(move_component) { move_component=false;++*replacement_root;loader_value=other_type; }
  ++component_calls;return RegisterFindClass(cache,components.at(array),false);
}
const void* ModernComponent(void* cache,const void* array) {
  ++component_calls;return RegisterFromClass(cache,components.at(array));
}
bool Assign(const void* lhs,const void* rhs,bool strict,void* verifier) {
  Need(verifier==verifier_token,"component assignment changed the verifier");
  if(components.count(lhs)&&components.count(rhs))return array_answer;
  Need(strict,"array component assignment used lenient interface rules");return leaf_answer;
}
bool AssignModern(const void* verifier,const void* lhs,const void* rhs,bool strict) {
  return Assign(lhs,rhs,strict,const_cast<void*>(verifier));
}
int main() {
 try {
  using namespace symbols::verification_redirect;
  std::vector<Hook> hooks;std::string why;
  Need(Prepare(&hooks,&why)&&ResolvesClasses()&&ResolvesRegisterTypes()&&hooks.size()==9&&ResolvesArrayAssignability(),
       "complete legacy cache pair was not prepared");
  missing={kRegisterInsertClass.name,kRegisterFromClass.name};hooks.clear();
  Need(Prepare(&hooks,&why)&&!ResolvesRegisterTypes()&&hooks.size()==3,
       "partial legacy cache pair falsely enabled self arrays");
  missing={kRegisterFindClass.name,kRegisterInsertClass.name};hooks.clear();
  Need(Prepare(&hooks,&why)&&ResolvesRegisterTypes()&&hooks.size()==7&&ResolvesArrayAssignability(),
       "modern FromClass service was not prepared");
  for(const auto* service : {&kModernConstruct,&kModernAssignable,&kModernComponent,&kModernFrom}) {
    missing={kRegisterFindClass.name,kRegisterInsertClass.name,service->name};hooks.clear();
    Need(Prepare(&hooks,&why)&&ResolvesRegisterTypes()&&!ResolvesArrayAssignability()&&hooks.size()==4,
         "partial modern covariance services enabled parent changes");
  }
  for(const auto* service : {&kLegacyConstruct,&kLegacyAssignable,&kLegacyComponent,&kLegacyFrom,&kLegacyFromClass}) {
    missing={service->name};hooks.clear();
    Need(Prepare(&hooks,&why)&&ResolvesRegisterTypes()&&!ResolvesArrayAssignability()&&hooks.size()==5,
         "partial legacy covariance services enabled parent changes");
  }
  missing.clear();
  hooks.clear();Need(Prepare(&hooks,&why)&&ResolvesArrayAssignability(),"complete covariance services failed to recover");
  original_register_find_class=HookAddress(CacheFind);
  original_register_insert_class=HookAddress(CacheInsert);
  original_register_from_class=HookAddress(CacheFrom);
  original_find_class=HookAddress(Lookup);original_find_class_with_length=HookAddress(LookupLength);
  original_check_verification=HookAddress(Verify);
  Begin();mapping[old_type]=new_type;
  auto* root_page=mmap(reinterpret_cast<void*>(0x20000000),4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0);
  Need(root_page!=MAP_FAILED,"cannot allocate compressed fake ART holder");
  uint32_t root_ref=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(root_page));
  abi::arm64::RedefinitionDataHolderPrefix holder{{&root_ref}};
  replacement_root=&holder.array.Get<abi::arm64::ObjectArray>()->elements[RedefinitionDataIter::kSlotNewClassObject];
  RedefinitionDataIter data{0,&holder};Verification scope{&data,"Ltarget;",nullptr,false};
  auto* state=CurrentState();state->verifying=&scope;
  Need(RegisterClass(other_type)==other_type,"same-name foreign-loader class was remapped");
  Need(RegisterClass(old_type)==new_type,"old component was not remapped");
  state->verifying=nullptr;
  Need(RegisterClass(old_type)==old_type,"mapping escaped verification");state->verifying=&scope;
  ready=false;Need(RegisterClass(old_type)==old_type,"mapping ignored missing runtime proof");ready=true;
  for(uint32_t flags : {uint32_t{1},uint32_t{0x601},uint32_t{0x11}}) {
    access=flags;
    for(bool precise : {false,true}) {
      RegisterFindClass(nullptr,old_type,precise);
      Need(received==new_type&&received_precision==bool(flags&0x10),"legacy lookup retained old component precision");
      RegisterInsertClass(nullptr,"Ltarget;",old_type,precise);
      Need(received==new_type&&received_precision==bool(flags&0x10),"legacy insertion disagreed with lookup precision");
    }
  }
  RegisterFromClass(nullptr,old_type);Need(received==new_type,"modern cache retained old component");
  Verification group{&data,"Ltarget;",reinterpret_cast<interface_redefinition::Backend*>(1),true};
  state->verifying=&group;Need(RegisterClass(old_type)==new_type,"interface-group component was not remapped");
  state->verifying=&scope;
  Need(FindClass(nullptr,nullptr,"[Ltarget;",{})==other_type&&state->verifying==&scope,
       "array lookup failed to restore its verification scope");
  Need(FindClassWithLength(nullptr,nullptr,"[[Ltarget;",10,{})==other_type&&state->verifying==&scope,
       "length-bearing array lookup failed to restore its scope");
  Need(raw_array_lookups==2,"an array lookup was synthesized or skipped");
  abi::arm64::RedefinerPrefix driver{};ClassRedefinitionPrefix self{&driver,nullptr,nullptr};
  context.targets[&self].descriptor="Ltarget;";context.targets[&self].redirect_verification=true;
  for(bool api16 : {false,true})for(bool verified : {false,true}) {
    modern=api16;result=verified;access_ok=true;state->verifying=&scope;
    Need(CheckVerification(&self,data)==verified&&state->verifying==&scope,"verifier result or outer scope changed");
  }
  modern=false;result=true;access_ok=false;driver.result=0;
  Need(!CheckVerification(&self,data)&&driver.result==JVMTI_ERROR_INTERNAL&&state->verifying==&scope,
       "replacement flag probe failure committed a mismatched register graph");
  original_legacy_assignable=HookAddress(Assign);original_modern_assignable=HookAddress(AssignModern);
  legacy_component=LegacyComponent;modern_component=ModernComponent;
  original_legacy_from=HookAddress(FromLegacy);original_modern_from=HookAddress(FromModern);
  original_legacy_construct=HookAddress(ConstructLegacy);original_modern_construct=HookAddress(ConstructModern);
  access_ok=true;access=1;state->register_error=false;
  void* cache=reinterpret_cast<void*>(0x900);
  void* different_cache=reinterpret_cast<void*>(0x901);
  void* left_array=reinterpret_cast<void*>(0x410);
  void* right_array=reinterpret_cast<void*>(0x420);
  void* nested_left=reinterpret_cast<void*>(0x430);
  void* nested_right=reinterpret_cast<void*>(0x440);
  components={{left_array,other_type},{right_array,old_type},
              {nested_left,left_array},{nested_right,right_array}};
  auto query=[&](bool api16,const void* lhs,const void* rhs) {
    return api16 ? ModernAssignable(verifier_token,lhs,rhs,false) : LegacyAssignable(lhs,rhs,false,verifier_token);
  };
  for(bool api16 : {false,true}) {
    for(void* type : {left_array,right_array,nested_left,nested_right})RecordRegister(cache,type,type==nested_left||type==nested_right?2:1,true);
    array_answer=false;leaf_answer=true;
    Need(query(api16,left_array,right_array),"new parent rejected the replacement component");
    Need(query(api16,nested_left,nested_right),"nested covariance retained old components");
    array_answer=true;leaf_answer=false;
    Need(!query(api16,left_array,right_array),"removed parent retained stale covariance");
    mapping.clear();array_answer=false;leaf_answer=true;
    Need(!query(api16,left_array,right_array),"unmapped primitive widening changed native array rules");
    mapping[old_type]=new_type;
    RecordRegister(different_cache,right_array,1,true);component_calls=0;
    Need(!query(api16,left_array,right_array)&&component_calls==0,"mixed-cache query used the wrong arena");
    state->register_caches.erase(left_array);
    Need(!query(api16,left_array,right_array)&&component_calls==0,"untracked query guessed a cache");
  }
  state->verifying=nullptr;component_calls=0;array_answer=false;
  Need(!query(true,left_array,right_array)&&component_calls==0,"mapping escaped verification");
  state->verifying=&scope;
  for(bool api16 : {false,true}) {
    RecordRegister(cache,left_array,1,true);RecordRegister(different_cache,right_array,1,true);
    if(api16)ModernConstruct(cache,new_type,old_type,other_type,{},old_type,true,false);
    else LegacyConstruct(cache,old_type,true,other_type,false);
    Need(!state->register_caches.count(left_array)&&state->register_caches.count(right_array),
         "cache address reuse retained proofs or cleared another arena");
    RecordRegister(cache,left_array,1,false);RecordRegister(cache,right_array,1,true);
    component_calls=0;array_answer=false;
    Need(!query(api16,left_array,right_array)&&component_calls==0,"unresolved array descriptor supplied resolved proof");
  }
  Need(constructor_calls==2,"cache lifetime entry did not delegate exactly once");
  for(bool api16 : {false,true}) {
    ResetCache(cache);from_result=left_array;resolve_from=true;
    if(api16)ModernFrom(cache,"[Lforeign;");else LegacyFrom(cache,nullptr,"[Lforeign;",false);
    Need(state->register_caches.at(left_array).rank==1&&state->register_caches.at(left_array).resolved,
         "actual descriptor resolution did not prove its returned array");
    resolve_from=false;
    if(api16)ModernFrom(cache,"[Lforeign;");else LegacyFrom(cache,nullptr,"[Lforeign;",false);
    Need(state->register_caches.at(left_array).resolved,"same-lifetime cache hit lost resolved proof");
    ResetCache(cache);
    if(api16)ModernFrom(cache,"[Lforeign;");else LegacyFrom(cache,nullptr,"[Lforeign;",false);
    Need(!state->register_caches.at(left_array).resolved,"cache address reuse supplied stale resolved proof");
    ResetCache(cache);
  }
  RecordRegister(cache,left_array,1,true);RecordRegister(cache,right_array,1,true);
  { ResolutionLookup nested("Lforeign;");component_calls=0;array_answer=false;
    Need(!query(false,left_array,right_array)&&component_calls==0,"nested class loading used the outer verifier loader");
  }
  Need(state->lookup_depth==0,"class lookup scope leaked its depth");
  RecordRegister(cache,left_array,1,true);RecordRegister(cache,right_array,1,true);
  loader_reads=0;move_component=true;leaf_answer=true;
  Need(query(false,left_array,right_array)&&loader_reads==2,
       "component lookups did not independently re-read the rooted loader");
  loader_value=nullptr;loader_ok=false;component_calls=0;state->register_error=false;
  Need(!query(false,left_array,right_array)&&component_calls==0&&state->register_error,
       "missing loader proof supplied an unverified component lookup");
  loader_ok=true;state->register_error=false;
  {
    ResolutionScope resolution("[[Lforeign;");
    { ResolutionLookup outer("[[Lforeign;");
      { ResolutionLookup nested("[[Lforeign;");nested.Result(other_type); }
      outer.Result(nullptr);
    }
    Need(!resolution.Resolved(),"recursive resolution supplied the outer result");
    { ResolutionLookup wrong("[Lforeign;");wrong.Result(other_type); }
    Need(!resolution.Resolved(),"different descriptor supplied resolved proof");
    { ResolutionLookup right("[[Lforeign;");right.Result(other_type); }
    Need(resolution.Resolved(),"successful outer array resolution was not recorded");
    { ResolutionScope nested("[Lother;");
      { ResolutionLookup found("[Lother;");found.Result(nullptr); }
      Need(!nested.Resolved(),"nested resolution inherited its caller's proof");
    }
    Need(resolution.Resolved(),"nested resolution erased its caller's proof");
  }
  Need(!state->resolving,"resolution scope escaped verification");
  munmap(root_page,4096);
  active=false;Need(RegisterClass(old_type)==old_type,"mapping escaped the transaction");
  current_reads=0;
  Need(FindClass(nullptr,nullptr,"Lforeign;",{})==other_type&&current_reads==1,
       "quiescent class lookup repeated transaction TLS reads");
  current_reads=0;
  Need(FindClassWithLength(nullptr,nullptr,"Lforeign;",9,{})==other_type&&current_reads==1,
       "length-bearing class lookup repeated transaction TLS reads");
  std::cout<<checks<<" production verifier boundary checks PASS\n";
 } catch(const std::exception& e) { std::cerr<<e.what()<<"\n";return 1; }
}
'''


def main():
    helper = runpy.run_path(str(ROOT / 'tools/test-structural-policy-host.py'))
    include = helper['java_home'](None) / 'include'
    platform = 'darwin' if (include / 'darwin').is_dir() else 'linux'
    out = new_evidence_directory(ROOT / 'build/evidence/host/verification-redirect')
    with tempfile.TemporaryDirectory(dir=out) as temporary:
        work = Path(temporary)
        (work / 'android').mkdir()
        (work / 'android/log.h').write_text('#pragma once\n#define ANDROID_LOG_ERROR 6\n'
                                          'extern "C" int __android_log_write(int,const char*,const char*);\n')
        (work / 'shadowhook.h').write_text('#pragma once\nextern "C" {\n'
            'void* shadowhook_dlopen(const char*);void shadowhook_dlclose(void*);\n'
            'void* shadowhook_dlsym(void*,const char*);void* shadowhook_dlsym_dynsym(void*,const char*);}\n')
        (work / 'test.cc').write_text(SOURCE)
        command = [shutil.which('clang++') or shutil.which('c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                   '-I' + str(work), '-I' + str(ROOT / 'artdcevm/src/main/cpp'), '-I' + str(include),
                   '-I' + str(include / platform), '-I' + str(ROOT / 'third_party/jvmti'),
                   str(work / 'test.cc'), '-o', str(work / 'test')]
        if sys.platform == 'darwin':
            command.extend(['-Wl,-pagezero_size,0x1000'])
        result = subprocess.run(command, capture_output=True)
        (out / 'compile.log').write_bytes(result.stdout + result.stderr)
        if result.returncode: raise RuntimeError('Verifier host compilation failed; inspect ignored evidence')
        result = subprocess.run([str(work / 'test')], capture_output=True)
        (out / 'run.log').write_bytes(result.stdout + result.stderr)
        if result.returncode: raise RuntimeError('Verifier host checks failed; inspect ignored evidence')
        print(result.stdout.decode().strip())


if __name__ == '__main__':
    main()
