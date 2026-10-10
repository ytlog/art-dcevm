#!/usr/bin/env python3
"""Validate the actual fixed Android17 source carrier decoder, no readiness claim."""
import os, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
out=root/'build/evidence/host/native-checked-method-resolution17';out.mkdir(parents=True,exist_ok=True)
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
command=[os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),'-I',str(jdk/'include'),'-I',str(jdk/'include'/('darwin' if os.uname().sysname=='Darwin' else 'linux')),str(root/'tests/host/native_checked_method_resolution17_test.cc'),'-o',str(out/'test')]
result=subprocess.run(command,capture_output=True,text=True);(out/'compile.log').write_text(result.stdout+result.stderr);result.check_returncode()
result=subprocess.run([str(out/'test')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'result.log').write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
