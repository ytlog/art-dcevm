#!/usr/bin/env python3
"""Actual production owners composed with independent ART/JNI public leaf model.

This does not establish Android ABI, incoming/new method graph, GC relocation,
or native-boundary publication permission.
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import os,subprocess,sys,hashlib,json
from call_graph_host_fixture import prepare
r=Path(__file__).resolve().parents[1];cpp=r/'artdcevm/src/main/cpp';out=r/'build/evidence/host/native-call-graph-execution'
java,platform=prepare(r,out);sys.path.insert(0,str(r/'tests/host'))
from call_graph_execution_dex_fixture import write
fixture=write(out)
names=['native_call_graph_execution','native_call_graph_replacement','native_call_graph_incoming','native_call_graph_super','native_call_graph_inventory','native_call_graph_resolution','native_call_graph_dispatch','native_call_graph','native_call_graph_code','native_call_graph_stack','native_runtime_root_provenance','native_selective_interpretation','native_source_stop','native_source_class_loading','native_cache_service','method_identity_calibration','modern_field_metadata','native_interface_graph','field_copy','native_schema_guard_verifier','native_schema_policy','native_schema_binding','native_method_table','native_abstract_instances','native_interface_dispatch','native_interface_dispatch_domain','native_receiver_producer','native_receiver_bytecode','native_receiver_frame','redefinition_gate','native_method_evolution_policy','modern_method_services','native_hierarchy_graph']
units=[r/'tests/host/native_call_graph_execution_test.cc']+[cpp/'platform/common'/(n+'.cc') for n in names]+[cpp/'evolution/dex_schema.cc',cpp/'evolution/migration_plan.cc']
flags=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(r/'tests/host'),'-I',str(java/'include'),'-I',str(java/'include'/platform)]
# Keep Clang UBSan function prefixes with their functions on Mach-O: stripping
# callback atoms otherwise leaves the wrong type hash immediately before entry.
headers=b''.join(p.read_bytes() for p in sorted(cpp.rglob('*.h')))
objects=out/'objects';objects.mkdir(exist_ok=True)
manifest={str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in units}
manifest.update({str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (r/'tests/host').glob('*call_graph*fixture.h')})
(out/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
def compile_unit(p):
    obj=objects/(p.stem+'.o');stamp=obj.with_suffix('.sha256');key=hashlib.sha256(p.read_bytes()+headers+(b''.join(h.read_bytes() for h in sorted((r/'tests/host').glob('*call_graph*fixture.h'))) if p.parent==r/'tests/host' else b'')+' '.join(flags).encode()).hexdigest()
    if not obj.exists() or not stamp.exists() or stamp.read_text()!=key:
        result=subprocess.run(flags+['-c',str(p),'-o',str(obj)],capture_output=True,text=True);obj.with_suffix('.log').write_text(result.stdout+result.stderr);result.check_returncode();stamp.write_text(key)
    return str(obj)
with ThreadPoolExecutor(max_workers=3) as workers:compiled=list(workers.map(compile_unit,units))
link=[flags[0]]+compiled+['-fsanitize=address,undefined']
link+=['-Wl,-pagezero_size,0x10000'] if platform=='darwin' else ['-Wl,--gc-sections','-ldl']
result=subprocess.run(link+['-o',str(out/'test')],capture_output=True,text=True);(out/'link.log').write_text(result.stdout+result.stderr);result.check_returncode()
for case in ('drained','compiled','body','unpaused','flags','generation'):
    result=subprocess.run([str(out/'test'),str(fixture),case],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/('result-'+case+'.log')).write_text(result.stdout+result.stderr);result.check_returncode();print(case+': '+result.stdout.strip())
