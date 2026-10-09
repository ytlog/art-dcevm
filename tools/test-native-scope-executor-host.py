#!/usr/bin/env python3
"""Actual Native executor TU with independent normal-JNI and state boundary doubles."""
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/native-scope-executor';out.mkdir(parents=True,exist_ok=True)
cmd=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/native_scope_executor_test.cc'),'-o',str(out/'test')]
subprocess.run(cmd,check=True)
r=subprocess.run([str(out/'test')],capture_output=True,text=True);(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
