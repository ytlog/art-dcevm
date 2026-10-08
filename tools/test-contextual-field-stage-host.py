#!/usr/bin/env python3
import os, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
out=root/'build/evidence/host/contextual-field-stage';out.mkdir(parents=True,exist_ok=True)
java=Path(subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
binary=out/'stage_test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-Werror','-g','-O1','-fsanitize=address,undefined','-I'+str(root/'artdcevm/src/main/cpp'),'-I'+str(java/'include'),'-I'+str(java/'include/darwin'),str(root/'tests/host/contextual_field_stage_test.cc'),'-o',str(binary)],check=True)
r=subprocess.run([str(binary)],capture_output=True,text=True);(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
