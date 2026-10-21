#!/usr/bin/env python3
"""Exercise production verifier redirection with fake ART caches and rooted mappings."""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include <iostream>
#include <stdexcept>
#include <map>
#include <set>
#include "platform/common/verification_redirect.cc"
using namespace artdcevm;
using namespace artdcevm::verification_redirect;
using abi::arm64::Handle;
using abi::arm64::RedefinitionDataIter;
using abi::arm64::ClassRedefinitionPrefix;
transaction::Context context;
bool active=true, ready=true, access_ok=true, result=true, modern=false;
int checks=0, raw_array_lookups=0;
uint32_t access=1;
void* old_type=reinterpret_cast<void*>(0x100);
void* new_type=reinterpret_cast<void*>(0x200);
void* other_type=reinterpret_cast<void*>(0x300);
std::map<void*,void*> mapping;
std::set<std::string> missing;
void Need(bool condition,const char* why) { ++checks; if(!condition)throw std::runtime_error(why); }
namespace artdcevm::transaction {
Context* Current() noexcept { return active ? &context : nullptr; }
const Requirements* Context::Find(ClassRedefinitionPrefix* self) const {
  auto it=targets.find(self); return it==targets.end()?nullptr:&it->second;
}
}
namespace artdcevm::runtime_thread {
std::optional<VerifiedThread> Ready() noexcept {
  return ready ? std::optional<VerifiedThread>(VerifiedThread(nullptr)) : std::nullopt;
}
void* Current(const VerifiedThread& thread) noexcept { (void)thread.state_; return nullptr; }
const abi::arm64::RuntimeThread& Layout(const VerifiedThread&) noexcept {
  static abi::arm64::RuntimeThread layout{}; return layout;
}
}
namespace artdcevm::class_layout {
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
int main() {
 try {
  using namespace symbols::verification_redirect;
  std::vector<Hook> hooks;std::string why;
  Need(Prepare(&hooks,&why)&&ResolvesClasses()&&ResolvesRegisterTypes()&&hooks.size()==5,
       "complete legacy cache pair was not prepared");
  missing={kRegisterInsertClass.name,kRegisterFromClass.name};hooks.clear();
  Need(Prepare(&hooks,&why)&&!ResolvesRegisterTypes()&&hooks.size()==3,
       "partial legacy cache pair falsely enabled self arrays");
  missing={kRegisterFindClass.name,kRegisterInsertClass.name};hooks.clear();
  Need(Prepare(&hooks,&why)&&ResolvesRegisterTypes()&&hooks.size()==4,
       "modern FromClass service was not prepared");
  missing.clear();
  original_register_find_class=HookAddress(CacheFind);
  original_register_insert_class=HookAddress(CacheInsert);
  original_register_from_class=HookAddress(CacheFrom);
  original_find_class=HookAddress(Lookup);original_find_class_with_length=HookAddress(LookupLength);
  original_check_verification=HookAddress(Verify);
  Begin();mapping[old_type]=new_type;
  RedefinitionDataIter data{0,nullptr};Verification scope{&data,"Ltarget;",nullptr,false};
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
  active=false;Need(RegisterClass(old_type)==old_type,"mapping escaped the transaction");
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
        result = subprocess.run(command, capture_output=True)
        (out / 'compile.log').write_bytes(result.stdout + result.stderr)
        if result.returncode: raise RuntimeError('Verifier host compilation failed; inspect ignored evidence')
        result = subprocess.run([str(work / 'test')], capture_output=True)
        (out / 'run.log').write_bytes(result.stdout + result.stderr)
        if result.returncode: raise RuntimeError('Verifier host checks failed; inspect ignored evidence')
        print(result.stdout.decode().strip())


if __name__ == '__main__':
    main()
