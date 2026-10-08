#!/usr/bin/env python3
"""Check JNI ABI candidate selection from loaded runtime symbol capabilities."""
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
cpp=root/'artdcevm/src/main/cpp'
out=root/'build/evidence/host/jni-capability-selection'
out.mkdir(parents=True,exist_ok=True)
binary=out/'jni_capability_selection_test'
command=[os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),str(root/'tests/host/jni_capability_selection_test.cc'),str(cpp/'platform/jni_access/capability_selection.cc'),'-o',str(binary)]
subprocess.run(command,check=True)
result=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'host-test.log').write_text(result.stdout+result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('JNI capability selection: ASan/UBSan passed')
