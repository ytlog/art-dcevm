#!/usr/bin/env python3
"""Verify exclusive owned root lifetimes and all scope restoration paths."""
import os, subprocess, sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
cpp=root/'artdcevm/src/main/cpp'
jdk=Path(os.environ.get('JAVA_HOME','/Library/Java/JavaVirtualMachines/temurin-21.jdk/Contents/Home'))
out=root/'build/evidence/host/exclusive-owned-roots'
out.mkdir(parents=True,exist_ok=True)
binary=out/'exclusive_owned_roots_test'
command=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror',
         '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),
         '-I',str(jdk/'include'),'-I',str(jdk/'include'/('darwin' if sys.platform=='darwin' else 'linux')),
         str(root/'tests/host/exclusive_owned_roots_test.cc'),
         str(cpp/'platform/common/exclusive_owned_roots.cc'),
         str(cpp/'platform/common/runnable_jni_bridge.cc'),
         str(cpp/'platform/common/method_identity_calibration.cc'),'-pthread','-o',str(binary)]
subprocess.run(command,check=True)
result=subprocess.run([str(binary)],capture_output=True,text=True,
    env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'test.log').write_text(result.stdout+result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('Exclusive roots: passed (ASan/UBSan)')
