#!/usr/bin/env python3
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];out=root/'build/evidence/host/native-final-field-aliases';out.mkdir(parents=True,exist_ok=True)
binary=out/'aliases_test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-pthread','-I',str(root/'artdcevm/src/main/cpp'),str(root/'tests/host/native_final_field_aliases_test.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_final_field_aliases.cc'),'-o',str(binary)],check=True)
r=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
