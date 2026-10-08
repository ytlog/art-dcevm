#!/usr/bin/env python3
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/runnable-executor';out.mkdir(parents=True,exist_ok=True)
binary=out/'test'
sources=['platform/common/runnable_executor.cc','platform/common/runnable_jni_bridge.cc','platform/common/runnable_global_ref_bridge.cc','platform/jni_access/capability_selection.cc']
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/runnable_executor_test.cc')]+[str(cpp/x) for x in sources]+['-o',str(binary)],check=True)
logs=[]
for mode in ('global','fallback','missing','peer-mismatch'):
 result=subprocess.run([str(binary),mode],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
 logs.append(result.stdout+result.stderr);(out/'test.log').write_text(''.join(logs));result.check_returncode();print(result.stdout.strip())
