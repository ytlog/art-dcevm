#!/usr/bin/env python3
"""Actual Holder/root/method/interpretation owners with independent ART leaves."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import os, subprocess, sys, hashlib, json
from call_graph_host_fixture import prepare
r=Path(__file__).resolve().parents[1];cpp=r/'artdcevm/src/main/cpp'
out=r/'build/evidence/host/native-call-graph-replacement'
java,platform=prepare(r,out);sys.path.insert(0,str(r/'tests/host'))
from call_graph_execution_dex_fixture import write
before=write(out/'before');incoming=write(out/'incoming',incoming_extra=True)
abstract=write(out/'abstract',incoming_extra=True,abstract_keep=True)
names=['native_call_graph_replacement','native_call_graph_incoming','native_call_graph_super','native_call_graph_inventory','native_call_graph_resolution','native_call_graph_dispatch','native_call_graph','native_call_graph_code','native_call_graph_stack','native_selective_interpretation','native_source_stop','native_source_class_loading','native_cache_service','method_identity_calibration','modern_field_metadata','native_interface_graph','field_copy','native_schema_guard_verifier','native_schema_policy','native_schema_binding','native_method_table','native_abstract_instances','native_interface_dispatch','native_interface_dispatch_domain','native_receiver_producer','native_receiver_bytecode','native_receiver_frame','redefinition_gate','native_method_evolution_policy','modern_method_services','native_hierarchy_graph']
units=[r/'tests/host/native_call_graph_replacement_test.cc']+[cpp/'platform/common'/(n+'.cc') for n in names]+[cpp/'evolution/dex_schema.cc',cpp/'evolution/migration_plan.cc']
flags=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(r/'tests/host'),'-I',str(java/'include'),'-I',str(java/'include'/platform)]
headers=list(sorted(cpp.rglob('*.h')))+list(sorted((r/'tests/host').glob('*call_graph*fixture.h')))
sources=units+headers+[Path(__file__),r/'tests/host/call_graph_execution_dex_fixture.py']
manifest={str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
(out/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
header_bytes=b''.join(p.read_bytes() for p in headers)
objects=out/'objects';objects.mkdir(exist_ok=True)
def compile_unit(p):
 obj=objects/(p.stem+'.o');stamp=obj.with_suffix('.sha256')
 key=hashlib.sha256(p.read_bytes()+header_bytes+' '.join(flags).encode()).hexdigest()
 if not obj.exists() or not stamp.exists() or stamp.read_text()!=key:
  result=subprocess.run(flags+['-c',str(p),'-o',str(obj)],capture_output=True,text=True)
  obj.with_suffix('.log').write_text(result.stdout+result.stderr);result.check_returncode();stamp.write_text(key)
 return str(obj)
with ThreadPoolExecutor(max_workers=3) as workers:compiled=list(workers.map(compile_unit,units))
link=[flags[0]]+compiled+['-fsanitize=address,undefined']
link+=['-Wl,-pagezero_size,0x10000'] if platform=='darwin' else ['-ldl']
result=subprocess.run(link+['-o',str(out/'test')],capture_output=True,text=True)
(out/'link.log').write_text(result.stdout+result.stderr);result.check_returncode()
for case,path in (('concrete',incoming),('abstract',abstract)):
 result=subprocess.run([str(out/'test'),str(before),str(path),case],capture_output=True,text=True,
  env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
 (out/('result-'+case+'.log')).write_text(result.stdout+result.stderr);result.check_returncode()
 print(case+': '+result.stdout.strip())
assert all(hashlib.sha256((r/p).read_bytes()).hexdigest()==value for p,value in manifest.items())
