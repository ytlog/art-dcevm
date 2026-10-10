#!/usr/bin/env python3
"""Run real calibrated metadata plus native static alias owner against ART boundary doubles."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parents[1]
out=root/'build/evidence/host/native-static-field-aliases';out.mkdir(parents=True,exist_ok=True)
original=(root/'tests/host/native_source_commit_test.cc').read_text()
# Reuse the independently calibrated JNI/storage fixture, not private service state.
source=original[:original.index(' native_cache::Service caches;',original.index('int main()'))]
source=source.replace('#include "platform/common/native_source_commit.h"','#include "platform/common/native_source_commit.h"\n#include "platform/common/native_static_field_aliases.h"')
source=source.replace('void* Current(){','std::unordered_map<void*,void*> alias_history;\nabi::arm64::android11::ReleaseObjectPointer AliasHistory(void* c){auto i=alias_history.find(c);return {i==alias_history.end()?0:uintptr_t(i->second)};}\nvoid* Current(){')
source=source.replace('CALLEE("Class10Implements",Implements);','CALLEE("GetObsoleteClass",AliasHistory);CALLEE("Class10Implements",Implements);')
# Every independently constructed field allocation enters the boundary oracle.
source=source.replace('const char* FieldName(void* field){','std::unordered_map<void*,size_t> alias_field_oracle;\nconst char* FieldName(void* field){')
source=source.replace('return nullptr;}\nconst char* FieldType','auto i=alias_field_oracle.find(field);return i==alias_field_oracle.end()?nullptr:names[i->second];}\nconst char* FieldType',1)
source=source.replace('return nullptr;}\nvoid* FieldLeaf','auto i=alias_field_oracle.find(field);return i==alias_field_oracle.end()?nullptr:types[i->second];}\nvoid* FieldLeaf',1)
source=source.replace('*output[i]={uint32_t(uintptr_t(klass))','alias_field_oracle.emplace(output[i],i);*output[i]={uint32_t(uintptr_t(klass))')
source=source.replace('int main(){',"bool forbid_alias_allocation=false;\nvoid* operator new(size_t n){if(forbid_alias_allocation)std::abort();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}\nvoid operator delete(void* p)noexcept{std::free(p);}\nint main(){")
source+=(root/'tests/host/native_static_field_aliases_test.inc').read_text()
driver=out/'driver.cc';driver.write_text(source)
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
sources=['native_source_commit','native_initialized_transfer','modern_source_commit','source_commit','modern_field_metadata','native_interface_graph','modern_field_storage','field_copy','prepared_field_copy','exclusive_owned_roots','source_handle_roots','native_cache_service','modern_method_services','structural_methods','method_identity_calibration','object_replacement_map']
cmd=[os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(driver)]
cmd += [str(root/'artdcevm/src/main/cpp/platform/common'/f'{s}.cc') for s in sources]+[str(root/'artdcevm/src/main/cpp/evolution/dex_schema.cc')]
if platform=='darwin':cmd+=['-Wl,-pagezero_size,0x10000']
cmd+=['-o',str(out/'test')];subprocess.run(cmd,check=True)
r=subprocess.run([str(out/'test')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);r.check_returncode()
