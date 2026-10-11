#!/usr/bin/env python3
"""Real Session/ThreadList/selective owner and original StackVisitor host protocol."""
from pathlib import Path
import os,sys,subprocess
from call_graph_host_fixture import prepare
r=Path(__file__).resolve().parents[1];cpp=r/'artdcevm/src/main/cpp';out=r/'build/evidence/host/native-call-graph-stack'
java,platform=prepare(r,out)
flags=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(out),'-I',str(r/'tests/host'),'-I',str(java/'include'),'-I',str(java/'include'/platform)]
names=['native_call_graph_stack','native_runtime_root_provenance','native_selective_interpretation','native_source_stop','native_source_class_loading','native_cache_service','method_identity_calibration','modern_field_metadata','native_interface_graph','field_copy']
units=[r/'tests/host/native_call_graph_stack_test.cc']+[cpp/'platform/common'/(n+'.cc') for n in names]
units.append(cpp/'evolution/dex_schema.cc')
exe=out/'test';cmd=flags+[str(p) for p in units]+['-o',str(exe)]
if platform=='darwin':cmd+=['-Wl,-pagezero_size,0x10000']
with (out/'test.log').open('w') as log:
 subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
 subprocess.run([str(exe)],stdout=log,stderr=subprocess.STDOUT,check=True)
print('Real selective owner, Session, ThreadList and StackVisitor drain host protocol PASS')
