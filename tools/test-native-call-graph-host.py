#!/usr/bin/env python3
"""Ordinary immutable graph planning, namespaced identity and bounded traversal."""
import hashlib, os, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
cpp=root/'artdcevm/src/main/cpp'
out=root/'build/evidence/host/native-call-graph';out.mkdir(parents=True,exist_ok=True)
binary=out/'native-call-graph-test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),str(root/'tests/host/native_call_graph_test.cc'),str(cpp/'platform/common/native_call_graph.cc'),'-o',str(binary)],check=True)
r=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
log=out/'result.log';log.write_text(r.stdout+r.stderr+f'Exit: {r.returncode}\n');r.check_returncode()
print('Native call graph reverse ancestors + forward closure, loader/DEX identities, dynamic domains, native boundaries, incomplete-edge refusals and 100000-node cycle ASan/UBSan PASS; no ART mutation/frame permission')
print('Log SHA-256:',hashlib.sha256(log.read_bytes()).hexdigest())
