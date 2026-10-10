#!/usr/bin/env python3
"""Instantiate the actual RunState aggregate with calibrated production services.
Only ART/JNI leaf receivers, callbacks and public graphs are boundary doubles.
"""
import os
import sys
import re
import hashlib
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
out=root/'build/evidence/host/native-schema-run-state';out.mkdir(parents=True,exist_ok=True)
java=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
sys.path.insert(0,str(root/'tests/host'))
from schema_run_state_dex_fixture import write_graph_fixture
graph_input=Path(os.environ.get('SOURCE_DEX_FIXTURE',str(root/'app/build/generated/patchAssets/hier-parent/com/github/ytlog/artdcevm/fixture/HierarchySubject.dex')))
graph_package=re.search(rb'(L(?:[A-Za-z0-9_$]+/)+)HierarchyInserted;\0',graph_input.read_bytes()).group(1).decode() if graph_input.exists() else 'Lcom/example/'
graph_dex=write_graph_fixture(out/'baseline-graph',graph_package)
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
commit=(root/'tests/host/native_source_commit_test.cc').read_text().split('int main(){')[0].replace('std::array<unsigned char,200>','std::array<unsigned char,512>')
(out/'native_commit_fixture.inc').write_text(commit)
provider=(root/'tests/host/native_member_provider_test.cc').read_text().split('int main(int argc,char** argv){')[0]
(out/'provider_fixture.inc').write_text(provider)
roots=(root/'tests/host/source_handle_roots_test.cc').read_text()
root_prefix=roots.split('int main(){')[0].replace('#include "native_commit_fixture.inc"','').replace('class RootHooks final:public Hooks','class RootHooks:public ProviderHooks').replace('return Hooks::FindSymbol(lib,name);','return ProviderHooks::FindSymbol(lib,name);')
(out/'source_cache_walker_fixture.inc').write_text(root_prefix)
setup=roots.split('int main(){',1)[1].split(' struct Use{')[0].replace('RootHooks hooks;','SourceHooks hooks;').replace('InitializeHeap();','InitializeHeap();InterfaceFixtures();').replace('return r.metadata->Calibrate(c,reinterpret_cast<jobject>(old_class),"LFixture;",r.oracle,4,r.error);','const bool ok=r.metadata->Calibrate(c,reinterpret_cast<jobject>(old_class),"LFixture;",r.oracle,4,r.error);if(!ok)std::cerr<<*r.error<<std::endl;return ok;')
(out/'root_setup.inc').write_text(setup)
pipeline=(root/'tests/host/native_hierarchy_pipeline_test.cc').read_text()
prefix=pipeline.split('#include "source_cache_calibration_main.inc"')[0]
start=prefix.index('static bool forbid_allocation=');end=prefix.index('using namespace artdcevm::native_hierarchy_graph;',start)
prefix=prefix[:start]+'static bool& forbid_allocation=forbid_provider_allocation;\n'+prefix[end:]
prefix=prefix.replace('&&!Read32(loader)', '&&loader&&Read32(loader)==old_class->class_loader').replace('default:return nullptr;}}','default:return reinterpret_cast<jobject>(uintptr_t(Read32(p,offsetof(class_metadata::Class11,class_loader))));}}').replace('assert(!va_arg(args,jobject));','(void)va_arg(args,jobject);').replace('const char*SourceDescriptor(void*p,void*){','const char*SourceDescriptor(void*p,void* storage){if(subject_descriptor.empty())return ResolutionDescriptor(p,storage);').replace('class SessionHooks final:','class SessionHooks:').replace('struct Array{','struct PublicInterfaceArray{').replace('new Array{','new PublicInterfaceArray{').replace('reinterpret_cast<Array*>','reinterpret_cast<PublicInterfaceArray*>')
(out/'pipeline_fixture.inc').write_text(prefix)
cleanup=(root/'tests/host/native_required_cleanup_test.cc').read_text().split('int main(int argc,char** argv){')[0]
cleanup=cleanup.replace('if(missing&&std::strstr(name,missing))','if(Hooks::missing&&std::strstr(name,Hooks::missing))').replace('#include "provider_fixture.inc"','').replace('class OwnerHooks:public ProviderHooks','class OwnerHooks:public SessionHooks').replace('return ProviderHooks::FindSymbol(lib,name);','return SessionHooks::FindSymbol(lib,name);').replace('return ProviderHooks::Install(target,proxy,original,e);','if(target==reinterpret_cast<void*>(Vendor))return ProviderHooks::Install(target,proxy,original,e);return SessionHooks::Install(target,proxy,original,e);')
(out/'cleanup_fixture.inc').write_text(cleanup)
body=pipeline.split('#include "source_cache_calibration_main.inc"',1)[1]
body=body.split('  native_hierarchy_source_cache::Mapper absent_mapper',1)[0]
body=body.replace('runtime.runtime=&linker;','runtime.runtime=&::runtime;').replace('native_hierarchy_source_cache::Composite caches(mapper,cache_owner)','native_hierarchy_source_cache::Composite hierarchy_caches(mapper,cache_owner)')
# Public interface fixtures replace JNI constructor lookups; compose the actual
# private monitor's Object constructor independently for its calibration only.
monitor='assert(allocation.CalibrateRetirement(context,reinterpret_cast<jobject>(old_class),&error));'
assert body.count(monitor)==1
body=body.replace(monitor,'{auto prior_find=table.FindClass;auto prior_method=table.GetMethodID;table.FindClass=Find;table.GetMethodID=PrivateMethod;'+monitor+'table.FindClass=prior_find;table.GetMethodID=prior_method;}')
(out/'pipeline_setup.inc').write_text(body)
(out/'android').mkdir(exist_ok=True);(out/'android/log.h').write_text('#pragma once\n#define ANDROID_LOG_ERROR 6\n#define ANDROID_LOG_INFO 4\ninline int __android_log_print(int,const char*,const char*,...){return 0;}\n')
sources=['native_boot_method_catalog','native_boot_aot_source','native_receiver_producer','native_schema_run_state','native_hierarchy_private_ledger','native_source_dex_registry','native_source_class_loading','native_hierarchy_session','native_class_lookup','native_hierarchy_source_checks','native_hierarchy_graph','native_hierarchy_allocator','native_hierarchy_receiver','rooted_hierarchy_allocator','native_hierarchy_cache','native_hierarchy_source_cache','whole_process_frames','native_required_cleanup','native_source_stop','native_member_provider','native_method_table','native_structural_reflection','resolution_invalidation_plan','native_source_commit','native_initialized_transfer','modern_source_commit','source_commit','modern_field_metadata','native_interface_graph','modern_field_storage','field_copy','prepared_field_copy','native_schema_policy','native_schema_binding','native_method_evolution_policy','source_handle_roots','exclusive_owned_roots','native_cache_service','modern_method_services','structural_methods','method_identity_calibration','object_replacement_map','native_owned_removal','native_removal_policy','native_schema_guard_verifier','owned_structural_transaction','source_allocation_transaction','native_abstract_instances']
fixture=Path(os.environ.get('SOURCE_DEX_FIXTURE',str(root/'app/build/generated/patchAssets/hier-parent/com/github/ytlog/artdcevm/fixture/HierarchySubject.dex')))
if not fixture.exists():raise SystemExit('SOURCE_DEX_FIXTURE must name the verified hierarchy fixture DEX')
common=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O0','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(root/'tests/host'),'-DRUN_STATE_GRAPH_DEX_FIXTURE='+chr(34)+str(graph_dex)+chr(34),'-I',str(java/'include'),'-I',str(java/'include'/platform),'-DPIPELINE_DEX_FIXTURE='+chr(34)+str(fixture)+chr(34)]
units=[root/'tests/host/native_schema_run_state_test.cc']+[cpp/'platform/common'/(s+'.cc') for s in sources]+[cpp/'evolution/dex_schema.cc',cpp/'evolution/migration_plan.cc',cpp/'platform/common/schema_snapshot.cc']
headers=b''.join(p.read_bytes() for p in sorted(cpp.rglob('*.h')))+b''.join(p.read_bytes() for p in sorted(out.glob('*.inc')))
headers+=(root/'tests/host/native_schema_run_state_mapping_fixture.h').read_bytes()
objects=out/'objects';objects.mkdir(exist_ok=True)
def compile_unit(source):
    obj=objects/(source.stem+'.o'); stamp=obj.with_suffix('.sha256')
    key=hashlib.sha256(source.read_bytes()+headers+' '.join(common).encode()).hexdigest()
    if not obj.exists() or not stamp.exists() or stamp.read_text()!=key:
        subprocess.run(common+['-c',str(source),'-o',str(obj)],check=True)
        stamp.write_text(key)
    return str(obj)
with ThreadPoolExecutor(max_workers=4) as pool:
    compiled=list(pool.map(compile_unit,units))
exe=out/'native_schema_run_state_test';link=common+compiled
if platform=='darwin':link+=['-Wl,-pagezero_size,0x10000']
subprocess.run(link+['-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
