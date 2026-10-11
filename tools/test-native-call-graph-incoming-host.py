#!/usr/bin/env python3
"""Exercise actual incoming DEX inventory against independent executable DEX."""
import os, subprocess, sys, hashlib, json
from pathlib import Path
r=Path(__file__).resolve().parents[1]
cpp=r/'artdcevm/src/main/cpp'
out=r/'build/evidence/host/native-call-graph-incoming';out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(r/'tests/host'))
from call_graph_execution_dex_fixture import write
fixture=write(out)
added=write(out/'added',incoming_extra=True)
# Reuse the independent container fixture and its own declaration regression.
subprocess.run([sys.executable,str(r/'tools/test-dex-container-input-host.py')],check=True)
container=r/'build/evidence/host/dex-container-input/subject-041.dex'
units=[r/'tests/host/native_call_graph_incoming_test.cc',cpp/'evolution/dex_schema.cc',
       cpp/'platform/common/native_call_graph_incoming.cc',cpp/'platform/common/native_call_graph_code.cc',
       cpp/'platform/common/native_receiver_bytecode.cc',cpp/'platform/common/native_receiver_frame.cc']
sources=units+[cpp/'evolution/class_schema.h',cpp/'platform/common/native_call_graph_incoming.h',
 r/'tools/test-native-call-graph-incoming-host.py',r/'tests/host/call_graph_execution_dex_fixture.py']
manifest={str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
binary=out/'test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror',
 '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),*[str(p)for p in units],'-o',str(binary)],check=True)
result=subprocess.run([str(binary),str(fixture),str(container),str(added)],capture_output=True,text=True,
 env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(result.stdout+result.stderr);result.check_returncode()
assert all(hashlib.sha256((r/p).read_bytes()).hexdigest()==value for p,value in manifest.items())
(out/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(result.stdout.strip())
