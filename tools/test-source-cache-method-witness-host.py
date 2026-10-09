#!/usr/bin/env python3
"""Run production cache Method and Field witness checks under ASan/UBSan."""
import os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[1]
jdk=pathlib.Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
out=root/'build/evidence/host/source-cache-method-witness';out.mkdir(parents=True,exist_ok=True)
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
for name in ('method','field'):
 binary=out/(name+'-test')
 subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-Werror','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/f'tests/host/source_cache_{name}_witness_test.cc'),'-o',str(binary)],check=True)
 result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
 (out/(name+'-result.log')).write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
