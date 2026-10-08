#!/usr/bin/env python3
"""Exercise native cache owner observation and stopped-world enumeration."""
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
cpp=root/'artdcevm/src/main/cpp'
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/native-cache-service';out.mkdir(parents=True,exist_ok=True)
binary=out/'native_cache_service_test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/native_cache_service_test.cc'),str(cpp/'platform/common/native_cache_service.cc'),str(cpp/'platform/common/method_identity_calibration.cc'),'-o',str(binary)],check=True)
r=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip());print('Native cache: ASan/UBSan passed')
