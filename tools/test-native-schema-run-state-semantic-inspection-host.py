#!/usr/bin/env python3
"""Actual RunState semantic calibration through exclusive scoped inspection."""
from pathlib import Path
import os
import subprocess
base=Path(__file__).with_name('test-native-schema-run-state-success-host.py').resolve()
ns={'__file__':str(base)}
# Reuse the independently generated before/after DEX and actual JNI/native
# fixture composition, without replacing any production permission reader.
prelude=base.read_text().split('common=[',1)[0]
prelude=prelude.replace("baseline.read_text().split('fixture=Path',1)[0]", "baseline.read_text().replace(\"out=root/'build/evidence/host/native-schema-run-state'\", \"out=root/'build/evidence/host/native-schema-semantic-inspection'\").split('fixture=Path',1)[0]")
exec(compile(prelude,str(base),'exec'),ns)
root=ns['root'];cpp=ns['cpp'];out=ns['out'];java=ns['java'];platform=ns['platform'];sources=ns['sources']
body=(root/'tests/host/native_schema_run_state_success_test.cc').read_text().replace('int main(){','static void RunWaitingTransaction(){')
body=body.replace('ConfigurePublicGuard(table,funcs);','ConfigurePublicGuard(table,funcs);table.NewLocalRef=Global;')
body=body.replace('#include "native_schema_run_state_public_fixture.h"','#include "native_schema_run_state_public_fixture.h"\n#include "native_schema_run_state_waiting_hooks.h"')
body=body.replace('run->Refund();class_remove_proxy', 'assert(Read32(thread.data())==0&&!gc&&!stopped);run->Refund();class_remove_proxy')
body=body.replace('verifier.Clear(&env);return false;', 'verifier.Clear(&env);assert(std::strcmp(waiting_case,"valid"));return true;')
body=body.replace('  matched_plan=&plan;', '  assert(!std::strcmp(waiting_case,"valid")&&Read32(thread.data())==0&&gc);matched_plan=&plan;')
body=body.replace('#include "native_schema_run_state_public_fixture.h"', '#include "native_schema_run_state_public_fixture.h"\n#include "native_schema_semantic_inspection_fixture.h"')
needle='  auto run=native_schema_run_state::RunState::Begin(deps,&actual_driver,aggregate_context,&ti,{proposal},witness,&error);'
assert needle in body
body=body.replace(needle, '  native_cache_semantic::Service semantic;native_cache_semantic::HostFixture::Bind(semantic,*d.metadata,*deps.members.fields);semantic_type_index=witness.type_index;semantic_type_count=witness.counts.types;semantic_native_field=witness.native_field;semantic_native_method=witness.native_method;witness.semantic=&semantic;proposal.strong_original=nullptr;\n'+needle)
needle='  if(!run){std::cerr<<"matched Begin: "<<error<<"\\n";std::abort();}'
assert needle in body
body=body.replace(needle, '  assert(!run&&!native_schema_run_state::RunState::Current()&&!gc&&!stopped&&Read32(thread.data())==0);std::cerr<<"semantic Begin: "<<error<<" visits="<<semantic_visits<<"\\n";assert(semantic.Calibrated()==!std::strcmp(waiting_case,"valid"));assert(semantic_visits==(!std::strcmp(waiting_case,"valid")?1u:0u));verifier.Clear(&env);assert(SchemaTypedGlobals()==typed_globals_before_begin);assert(schema_typed_local_refs>0);return true;')
(out/'waiting_success_body.inc').write_text(body)
setup=(out/'aggregate_owner_setup.inc').read_text().replace('MatchedHooks owner_hooks;', 'WaitingHooks owner_hooks;')
setup=setup.replace('return owner_hooks.FindSymbol("libart.so",n);', 'if(!std::strcmp(n,abi::arm64::android13::kToNative)||!std::strcmp(n,abi::arm64::android13::kToRunnable))return static_cast<void*>(nullptr);return owner_hooks.FindSymbol("libart.so",n);')
needle='OwnerNative(thread.data());assert(native_source_stop::Install(owner_hooks,&error));OwnerRunnable(thread.data());'
assert needle in setup
setup=setup.replace(needle,'OwnerNative(thread.data());assert(native_source_stop::Install(owner_hooks,&error));assert(native_source_stop::InstallOriginalWaitingScope(owner_hooks,&error));OwnerRunnable(thread.data());')
needle='int original_run;'
setup=setup.replace(needle,'assert(!states.CanTransitionRunnable()&&source.RequiresOriginalWaiting());assert(source.CalibrateOriginalWaiting(thread.data(),thread.size(),&error));'+needle)
setup=setup.replace('RunStateMappings::Refresh,RunStateMappings::Read', 'SemanticMappings,RunStateMappings::Read')
setup=setup.replace('out->public_types=p.cache->types;', 'out->public_types=p.cache->types;out->public_types_array=p.cache->types_array;')
(out/'aggregate_owner_setup.inc').write_text(setup)
commit=(out/'native_commit_fixture.inc').read_text().replace('void CachePrepare(){assert(!stopped);lock_proxy(&mutex,current,lock_argument);}', 'void CachePrepare(){assert(!stopped);ExecuteWaitingCachePrepare();}')
(out/'native_commit_fixture.inc').write_text(commit)
cleanup=(out/'cleanup_fixture.inc').read_text()
old='void OwnerStart(void* h,void* p,int32_t cause,int32_t collector){assert(h==&owner_heap&&p==thread.data()&&cause==8&&collector==1&&!gc&&!stopped);gc=true;}'
assert old in cleanup
cleanup=cleanup.replace(old,'void OwnerStart(void* h,void* p,int32_t cause,int32_t collector){ExecuteWaitingStart(h,p,cause,collector);}')
cleanup=cleanup.replace('type==0&&gc&&!stopped);gc=false;', 'type==0&&gc&&!stopped);ExecuteWaitingFinish(h,p);gc=false;')
cleanup=cleanup.replace('Read32(thread.data())==92u<<24&&!stopped', '(Read32(thread.data())==92u<<24||Read32(thread.data())==74u<<24)&&!stopped')
cleanup=cleanup.replace('Read32(thread.data())==92u<<24&&stopped==1', '(Read32(thread.data())==92u<<24||Read32(thread.data())==74u<<24)&&stopped==1')
(out/'cleanup_fixture.inc').write_text(cleanup)
common=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O0','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(root/'tests/host'),'-I',str(java/'include'),'-I',str(java/'include'/platform),'-DPIPELINE_DEX_FIXTURE="'+str(ns['after'])+'"','-DRUN_STATE_BEFORE_DEX_FIXTURE="'+str(ns['before'])+'"','-DRUN_STATE_GRAPH_DEX_FIXTURE="'+str(ns['graph_dex'])+'"']
units=[root/'tests/host/native_schema_run_state_semantic_inspection_test.cc']+[cpp/'platform/common'/(s+'.cc') for s in sources]+[cpp/'evolution/dex_schema.cc',cpp/'evolution/migration_plan.cc',cpp/'platform/common/schema_snapshot.cc']
baseline=ns['baseline']
cache=baseline.read_text().split("headers=b''.join",1)[1].split("exe=out/'",1)[0]
exec(compile(("headers=b''.join"+cache).replace("objects=out", "headers+=(root/'tests/host/native_schema_run_state_public_fixture.h').read_bytes()+(root/'tests/host/native_schema_run_state_waiting_hooks.h').read_bytes()+(root/'tests/host/native_schema_semantic_inspection_fixture.h').read_bytes()\nobjects=out"),str(baseline),'exec'),dict(ns["namespace"],**ns,common=common,units=units))
exe=out/'native_schema_run_state_semantic_inspection_test'
link=common+[str(out/'objects'/(p.stem+'.o')) for p in units]
if platform=='darwin':link+=['-Wl,-pagezero_size,0x10000']
subprocess.run(link+['-o',str(exe)],check=True)
for case in ('valid','refuse','throw'):
 result=subprocess.run([str(exe),case],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 (out/f'waiting-{case}.log').write_text(result.stdout+result.stderr)
 result.check_returncode();print(result.stdout.strip())
