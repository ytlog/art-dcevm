#!/usr/bin/env python3
"""Check observation hooks in real public-JNI-shaped calls, with sanitizers."""
import os, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
cpp=root/'artdcevm/src/main/cpp'
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/runtime-receiver-probe';out.mkdir(parents=True,exist_ok=True)
binary=out/'runtime_receiver_probe_test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/runtime_receiver_probe_test.cc'),str(cpp/'platform/common/runtime_receiver_probe.cc'),'-o',str(binary)],check=True)
logs=[]
for mode in ('success','missing','bypassed','conflict','length','length-bypassed'):
 result=subprocess.run([str(binary),mode],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
 logs.append(result.stdout+result.stderr)
 (out/'test.log').write_text(''.join(logs))
 result.check_returncode()
 print(result.stdout.strip())
