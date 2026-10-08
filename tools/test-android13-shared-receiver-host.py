#!/usr/bin/env python3
"""Verify source13 consumes one shared observation owner and keeps its contract."""
import os, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
cpp=root/'artdcevm/src/main/cpp'
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/android13-shared-receiver';out.mkdir(parents=True,exist_ok=True)
binary=out/'android13_shared_receiver_test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/android13_shared_receiver_test.cc'),str(cpp/'platform/android13/runtime_receiver_probe.cc'),'-o',str(binary)],check=True)
result=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'test.log').write_text(result.stdout+result.stderr)
print(result.stdout,end='');result.check_returncode()
