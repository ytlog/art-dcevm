#!/usr/bin/env python3
"""Run production ordinary receiver input leaves with every failure boundary."""
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
out=root/'build/evidence/host/public-declared-field-mirror';out.mkdir(parents=True,exist_ok=True)
binary=out/'test'
command=[os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),'-I',str(jdk/'include'),'-I',str(jdk/'include'/('darwin' if os.uname().sysname=='Darwin' else 'linux')),str(root/'tests/host/public_declared_field_mirror_test.cc'),'-o',str(binary)]
subprocess.run(command,check=True)
result=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'host-test.log').write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
