#!/usr/bin/env python3
"""Check the production JIT release hooks, including GC and thread isolation."""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
from evidence_paths import new_evidence_directory
ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/common/jit_lifecycle.cc"
#include <cassert>
#include <thread>
#include <iostream>
using namespace artdcevm;
using namespace artdcevm::jit_lifecycle;
int api=36, missing=0, frees=0;
const void* code=reinterpret_cast<void*>(16);
const void* other=reinterpret_cast<void*>(32);
bool expect_removed=false, nested=false;
Cache* alternate=nullptr;
extern "C" int android_get_device_api_level(){return api;}
extern "C" void* shadowhook_dlopen(const char*){return reinterpret_cast<void*>(1);}
extern "C" void shadowhook_dlclose(void*){}
extern "C" void* shadowhook_dlsym(void*,const char* n){
  if((missing==1&&std::string(n).find("RemoveMethodLocked")!=std::string::npos)||
     (missing==2&&std::string(n).find("FreeCodeAndData")!=std::string::npos))return nullptr;
  return reinterpret_cast<void*>(1);
}
extern "C" void* shadowhook_dlsym_dynsym(void* h,const char* n){return shadowhook_dlsym(h,n);}
void OriginalFree(void* cache,const void* value){
  ++frees;assert(value==code);auto* s=static_cast<Cache*>(cache);
  assert((s->zombie_code.count(value)==0)==expect_removed);
  assert((s->processed_zombie_code.count(value)==0)==expect_removed);
  assert(s->zombie_code.count(other)==1&&s->processed_zombie_code.count(other)==1);
}
bool OriginalRemove(void* cache,void* method,bool release){
  assert(method==reinterpret_cast<void*>(64));
  if(nested){
    nested=false;
    bool prior=expect_removed;expect_removed=false;
    // A different thread and a different cache must not inherit release scope.
    std::thread t([&]{FreeCode(cache,code);});t.join();
    FreeCode(alternate,code);
    assert(!RemoveMethod(alternate,method,false));
    expect_removed=prior;
  }
  if(release)FreeCode(cache,code);
  return release;
}
void Seed(Cache& c){c.method_code[code]=reinterpret_cast<void*>(64);
  c.zombie_code={code,other};c.processed_zombie_code={code,other};}
int main(){
  std::vector<Hook> hooks;std::string failure;
  for(int level:{33,35,37}){api=level;assert(Prepare(&hooks,&failure)&&hooks.empty());}
  api=36;
  for(int m:{1,2}){missing=m;assert(!Prepare(&hooks,&failure)&&hooks.empty());}
  missing=0;assert(Prepare(&hooks,&failure)&&hooks.size()==2);
  original_remove=OriginalRemove;original_free=OriginalFree;
  Cache c{},second{};Seed(c);Seed(second);alternate=&second;
  // Unscoped GC freeing is untouched, even for a code present in both sets.
  FreeCode(&c,code);assert(frees==1);
  assert(!RemoveMethod(&c,reinterpret_cast<void*>(64),false));assert(frees==1);
  expect_removed=true;nested=true;
  assert(RemoveMethod(&c,reinterpret_cast<void*>(64),true));assert(releasing_cache==nullptr);
  assert(c.method_code.count(code)==1); // ART, not this hook, owns mapping removal.
  assert(second.zombie_code.count(code)==1);
  // Reuse is legal after release; the original assertion would accept insertion.
  assert(c.zombie_code.insert(code).second);
  // A non-method/JNI allocation is outside this repair.
  Seed(c);c.method_code.clear();expect_removed=false;
  assert(RemoveMethod(&c,reinterpret_cast<void*>(64),true));
  assert(releasing_cache==nullptr);assert(frees==5);
  std::cout<<"JIT lifecycle release, reuse, GC, nested scope, thread isolation and admission PASS\n";
}
'''
def main():
    evidence=new_evidence_directory(ROOT/'build/evidence/host/jit-lifecycle')
    helper=runpy.run_path(str(ROOT/'tools/test-structural-policy-host.py'))
    include=helper['java_home'](None)/'include'
    platform='darwin' if (include/'darwin').is_dir() else 'linux'
    with tempfile.TemporaryDirectory(dir=evidence) as temp:
        work=Path(temp);(work/'android').mkdir()
        (work/'android/api-level.h').write_text('extern "C" int android_get_device_api_level();\n')
        (work/'shadowhook.h').write_text('extern "C" { void* shadowhook_dlopen(const char*); void shadowhook_dlclose(void*); void* shadowhook_dlsym(void*,const char*); void* shadowhook_dlsym_dynsym(void*,const char*); }\n')
        source=work/'test.cc';source.write_text(SOURCE)
        for name,flags in [('normal',[]),('sanitizers',['-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
            binary=work/name
            cmd=[shutil.which('clang++') or 'c++','-std=c++17','-Wall','-Wextra','-Werror','-pthread',*flags,'-I'+str(work),'-I'+str(ROOT/'artdcevm/src/main/cpp'),'-I'+str(include),'-I'+str(include/platform),'-I'+str(ROOT/'third_party/jvmti'),str(source),'-o',str(binary)]
            r=subprocess.run(cmd,capture_output=True,text=True);(evidence/(name+'-compile.log')).write_text(r.stdout+r.stderr)
            if r.returncode:raise RuntimeError('Compilation failed; inspect ignored evidence')
            r=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30);(evidence/(name+'.log')).write_text(r.stdout+r.stderr)
            if r.returncode:raise RuntimeError('Lifecycle regression failed; inspect ignored evidence')
            print(name+': '+r.stdout.strip())
    print('Evidence: '+str(evidence.relative_to(ROOT)))
if __name__=='__main__':main()
