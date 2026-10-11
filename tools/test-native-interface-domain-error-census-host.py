#!/usr/bin/env python3
"""Exercise the real read-only error census and bounded managed root store."""
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
out=root/'build/evidence/host/native-interface-domain-error-census'
out.mkdir(parents=True,exist_ok=True)
java=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
command=[os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-Werror','-g','-O0','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),'-I',str(java/'include'),'-I',str(java/'include'/platform),str(root/'tests/host/native_interface_domain_error_census_test.cc'),'-o',str(out/'test')]
with (out/'test.log').open('w') as log:
    subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
    subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True)
print('Read-only interface-domain error census ASan/UBSan passed')
