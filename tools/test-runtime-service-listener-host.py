#!/usr/bin/env python3
"""Production listener installer with independent named ABI catalog, ASan/UBSan."""
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
cpp=root/'artdcevm/src/main/cpp'
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/runtime-service-listener'
out.mkdir(parents=True,exist_ok=True)
binary=out/'test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/runtime_service_listener_test.cc'),str(cpp/'platform/common/runtime_heap_receiver.cc'),'-o',str(binary)],check=True)
logs=[]
for mode in ('enum','bool','legacy','all','missing-listener','missing-construct','missing-destroy','install-failure'):
 result=subprocess.run([str(binary),mode],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
 logs.append(result.stdout+result.stderr)
 (out/'result.txt').write_text(''.join(logs))
 result.check_returncode()
 print(result.stdout.strip())
