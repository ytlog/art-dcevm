#!/usr/bin/env python3
"""Exercise the production shared hook owner with exact legacy/modern exception ABIs."""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
from evidence_paths import new_evidence_directory
ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "platform/common/interpreter_events.h"
#include "platform/common/private_continuation.h"
#include "platform/common/bound_method_handle.h"
#include "platform/common/art_hook.h"
#include "platform/symbols/bound_method_handle_symbols.h"
#include "platform/symbols/private_continuation_symbols.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace artdcevm;
int api, mode, installs=0, pc_calls=0, moves=0, policy=0;
void* move_replacement=nullptr;
const void* incoming=nullptr;
const void* saved=reinterpret_cast<void*>(0x4000);
void Need(bool b,const char* m){if(!b)throw std::runtime_error(m);}
extern "C" int android_get_device_api_level(){return api;}
bool Legacy(void* self,void* frame,const void* instrumentation){
 Need(self==reinterpret_cast<void*>(1)&&frame==reinterpret_cast<void*>(2),"legacy receiver/frame ABI");
 incoming=instrumentation;++moves;return true;
}
bool Modern(void* self,void* frame,bool skip,bool skip_throw){
 Need(self==reinterpret_cast<void*>(1)&&frame==reinterpret_cast<void*>(2),"modern receiver/frame ABI");
 Need(skip==(policy==1)&&skip_throw==(policy==2),"lost independent skip flags");++moves;return true;
}
bool Pc(void*,const void*,const void*,uint32_t,const void*,void*){++pc_calls;return true;}
namespace artdcevm::bound_method_handle {
std::atomic<bool> pc_preparing{false};
extern "C" { std::atomic<void*> artdcevm_bound_entry_method{nullptr}; }
bool DispatchPc(interpreter_events::Pc p,void* s,const void* a,const void* f,uint32_t pc,const void* i,void* r){return p(s,a,f,pc,i,r);}
}
namespace artdcevm::private_continuation {
std::atomic<bool> pc_active{false}, direct_pc_active{false};
bool Event(void*,const void*,const void*,uint32_t,const void*,void*,bool passed){return passed;}
bool Exception(void* self,void* frame,bool,bool,bool* handled){
 *handled=mode==3||mode==4;
 if(mode==4)return interpreter_events::ReplayException(self,frame,saved);
 return mode==3;
}
}
extern "C" void* shadowhook_dlopen(const char*){return reinterpret_cast<void*>(1);}
extern "C" void shadowhook_dlclose(void*){}
extern "C" void* shadowhook_dlsym(void*,const char* name){
 using namespace symbols::bound_method_handle;
 if(!std::strcmp(name,symbols::private_continuation::kMoveLegacy.name))return mode==1?nullptr:HookAddress(Legacy);
 if(!std::strcmp(name,kMoveException.name))return mode==2?nullptr:HookAddress(Modern);
 for(const auto& n:{kDexPc00,kDexPc01,kDexPc10,kDexPc11})if(!std::strcmp(name,n.name))return HookAddress(Pc);
 return nullptr;
}
extern "C" void* shadowhook_dlsym_dynsym(void* h,const char* n){return shadowhook_dlsym(h,n);}
extern "C" void* shadowhook_hook_sym_addr(void* target,void* replacement,void** original){
 if(mode==5&&target!=HookAddress(Pc))return nullptr;
 ++installs;*original=target;if(target!=HookAddress(Pc))move_replacement=replacement;return target;
}
int main(int argc,char** argv){try{
 Need(argc==3,"api and case required");api=std::stoi(argv[1]);mode=std::stoi(argv[2]);
 Need(interpreter_events::Prepare(),"PC preparation failed");
 const bool legacy=api<=32;
 const bool available=mode!=5&&!(legacy&&mode==1)&&!(!legacy&&mode==2);
 Need(interpreter_events::ExceptionService()==available,"wrong ABI service selected");
 Need(std::string(interpreter_events::ExceptionAbi())==(!available?"unavailable":legacy?"legacy-instrumentation":"skip-flags"),"ABI report mismatch");
 const int before=installs;Need(interpreter_events::Prepare()&&installs==before,"duplicate shared hook owner");
 Need(installs==(available?2:1),"ICF aliases installed more than once");
 if(available){
  auto* self=reinterpret_cast<void*>(1);auto* frame=reinterpret_cast<void*>(2);
  if(legacy){
   auto call=reinterpret_cast<bool(*)(void*,void*,const void*)>(move_replacement);
   for(const void* ptr:{static_cast<const void*>(nullptr),saved}){
    incoming=reinterpret_cast<void*>(9);const int previous=moves;
    Need(call(self,frame,ptr),"legacy result lost");
    Need(moves==previous+(mode==3?0:1),"marker reached original handler");
    if(mode!=3)Need(incoming==(mode==4?saved:ptr),"legacy instrumentation pointer policy changed");
   }
  }else{
   auto call=reinterpret_cast<interpreter_events::Move>(move_replacement);
   for(policy=0;policy<3;++policy){
    if(mode==4&&policy)continue; // continuation replay intentionally restores full notifications.
    const int previous=moves;Need(call(self,frame,policy==1,policy==2),"modern result lost");
    Need(moves==previous+(mode==3?0:1),"marker reached modern handler");
   }
  }
 }
 std::cout<<"api "<<api<<" shared exception case "<<mode<<" PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
'''
def main():
    out = new_evidence_directory(ROOT/'build/evidence/host/interpreter-events')
    include = runpy.run_path(str(ROOT/'tools/test-structural-policy-host.py'))['java_home'](None)/'include'
    platform = 'darwin' if (include/'darwin').is_dir() else 'linux'
    with tempfile.TemporaryDirectory(dir=out) as temp:
        work=Path(temp);(work/'android').mkdir()
        (work/'android/api-level.h').write_text('#pragma once\nextern "C" int android_get_device_api_level();\n')
        (work/'shadowhook.h').write_text('#pragma once\nextern "C" {void* shadowhook_dlopen(const char*);void shadowhook_dlclose(void*);void* shadowhook_dlsym(void*,const char*);void* shadowhook_dlsym_dynsym(void*,const char*);void* shadowhook_hook_sym_addr(void*,void*,void**);}\n')
        source=work/'case.cc';source.write_text(SOURCE);binary=work/'case';native=ROOT/'artdcevm/src/main/cpp'
        args=[shutil.which('clang++') or shutil.which('c++'),'-std=c++17','-Wall','-Wextra','-Werror','-pthread','-I'+str(work),'-I'+str(native),'-I'+str(include),'-I'+str(include/platform),'-I'+str(ROOT/'third_party/jvmti'),str(source),str(native/'platform/common/interpreter_events.cc'),'-o',str(binary)]
        r=subprocess.run(args,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError('Shared event compilation failed; inspect ignored evidence')
        for api in [30,31,32,33,36]:
            for mode in range(6):
                r=subprocess.run([str(binary),str(api),str(mode)],capture_output=True,text=True);(out/f'api{api}-case{mode}.log').write_text(r.stdout+r.stderr)
                if r.returncode:raise RuntimeError(f'Shared event API {api} case {mode} failed; inspect ignored evidence')
    print('Shared interpreter events: 30 typed ABI, policy, ICF and failure cases PASS')
    print('Evidence: '+str(out.relative_to(ROOT)))
if __name__=='__main__':main()
