#!/usr/bin/env python3
"""Whole matched RunState with actual one-pair publication hooks and refusal cases."""
from pathlib import Path
import os
import signal
import subprocess
import sys
base=Path(__file__).with_name('test-native-schema-run-state-success-host.py').resolve()
ns={'__file__':str(base)}
# Reuse the independently generated before/after DEX and actual JNI/native
# fixture composition, without replacing any production permission reader.
exec(compile(base.read_text().split('common=[',1)[0],str(base),'exec'),ns)
root=ns['root'];cpp=ns['cpp'];out=ns['out'];java=ns['java'];platform=ns['platform'];sources=ns['sources']
body=(root/'tests/host/native_schema_run_state_success_test.cc').read_text()
assert body.count('int main(){')==1
body=body.replace('int main(){','static void RunSuccessfulTransaction(){')
body=body.replace('publication has Android17 no-inner-GC semantics','publication uses actual calibrated one-inner-GC pair')
publication='  publication_proxy(&linker,{uintptr_t(cache)},{uintptr_t(app_loader)});'
assert body.count(publication)==1
body=body.replace(publication, "  const auto publication_os_queries=method_identity::MappingOsQueries();forbid_provider_allocation=true;\n"+publication+"\n  assert(method_identity::MappingOsQueries()==publication_os_queries);forbid_provider_allocation=false;")
(out/'gc_publication_success_body.inc').write_text(body)
pipeline=(out/'pipeline_fixture.inc').read_text()
needle='void OriginalPublication(void*,abi::arm64::android11::ReleaseObjectPointer,abi::arm64::android11::ReleaseObjectPointer){++publication_calls;}'
assert needle in pipeline
pipeline=pipeline.replace(needle,needle[:-1]+'ExecuteInnerPublicationGC();}')
(out/'pipeline_fixture.inc').write_text(pipeline)
cleanup=(out/'cleanup_fixture.inc').read_text()
old='void OwnerStart(void* h,void* p,int32_t cause,int32_t collector){assert(h==&owner_heap&&p==thread.data()&&cause==8&&collector==1&&!gc&&!stopped);gc=true;}'
new='void OwnerStart(void* h,void* p,int32_t cause,int32_t collector){assert(h==&owner_heap&&p==thread.data()&&((cause==8&&collector==1)||(cause==12&&(collector==11||collector==13)))&&!gc&&!stopped);if(cause==12){++forwarded_publication_starts;raw_publication_gc=true;}gc=true;}'
assert old in cleanup
cleanup=cleanup.replace(old,new)
old='void OwnerFinish(void* h,void* p,int32_t type){assert(h==&owner_heap&&p==thread.data()&&type==0&&gc&&!stopped);gc=false;}'
new=old.replace('gc=false;','if(raw_publication_gc){++forwarded_publication_finishes;raw_publication_gc=false;}gc=false;')
assert old in cleanup
cleanup=cleanup.replace(old,new)
(out/'cleanup_fixture.inc').write_text(cleanup)
common=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O0','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(root/'tests/host'),'-I',str(java/'include'),'-I',str(java/'include'/platform),'-DPIPELINE_DEX_FIXTURE="'+str(ns['after'])+'"','-DRUN_STATE_BEFORE_DEX_FIXTURE="'+str(ns['before'])+'"']
units=[root/'tests/host/native_schema_run_state_gc_publication_test.cc']+[cpp/'platform/common'/(s+'.cc') for s in sources]+[cpp/'evolution/dex_schema.cc',cpp/'evolution/migration_plan.cc',cpp/'platform/common/schema_snapshot.cc']
baseline=ns['baseline']
cache=baseline.read_text().split("headers=b''.join",1)[1].split("exe=out/'",1)[0]
exec(compile(("headers=b''.join"+cache).replace("objects=out", "headers+=(root/'tests/host/native_schema_run_state_public_fixture.h').read_bytes()\nobjects=out"),str(baseline),'exec'),dict(ns["namespace"],**ns,common=common,units=units))
exe=out/'native_schema_run_state_gc_publication_test'
link=common+[str(out/'objects'/(p.stem+'.o')) for p in units]
if platform=='darwin':link+=['-Wl,-pagezero_size,0x10000']
subprocess.run(link+['-o',str(exe)],check=True)
cases=['valid','no-pair','finish-first','wrong-cause','wrong-collector','wrong-heap','wrong-self','missing-finish','double-start','unwind','wrong-finish','double-finish']
for collector in (11,13):
 for case in cases:
  result=subprocess.run([str(exe),case,str(collector)],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  (out/f'gc-publication-{collector}-{case}.log').write_text(result.stdout+result.stderr)
  if case=='valid':
   result.check_returncode();print(result.stdout.strip())
  elif result.returncode!=-signal.SIGABRT:
   raise SystemExit(f'{collector}/{case}: expected actual invariant SIGABRT, got {result.returncode}\n{result.stderr}')
  else:print(f'{collector}/{case}: actual production Scope refusal PASS')
