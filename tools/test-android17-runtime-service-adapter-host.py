#!/usr/bin/env python3
"""Check Android17 delegates calibration to the sole shared observer owner."""
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
java=Path(subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
out=root/'build/evidence/host/android17-runtime-service-adapter';out.mkdir(parents=True,exist_ok=True)
binary=out/'adapter_test'
command=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),'-I',str(java/'include'),'-I',str(java/'include/darwin'),str(root/'tests/host/android17_runtime_service_adapter_test.cc'),str(root/'artdcevm/src/main/cpp/platform/android17/runtime_services.cc'),'-o',str(binary)]
subprocess.run(command,check=True)
r=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
