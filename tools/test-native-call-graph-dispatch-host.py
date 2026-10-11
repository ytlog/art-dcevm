#!/usr/bin/env python3
import hashlib, os, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
out=root/'build/evidence/host/native-call-graph-dispatch'
out.mkdir(parents=True,exist_ok=True)
cpp=root/'artdcevm/src/main/cpp'
cmd=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I','/Library/Java/JavaVirtualMachines/temurin-21.jdk/Contents/Home/include','-I','/Library/Java/JavaVirtualMachines/temurin-21.jdk/Contents/Home/include/darwin',str(root/'tests/host/native_call_graph_dispatch_test.cc'),'-o',str(out/'test')]
r=subprocess.run(cmd,capture_output=True,text=True)
(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(out/'test')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
log=out/'result.log';log.write_text(r.stdout+r.stderr);r.check_returncode()
print(r.stdout.strip());print('Log SHA-256:',hashlib.sha256(log.read_bytes()).hexdigest())
