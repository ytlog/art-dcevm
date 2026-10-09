#!/usr/bin/env python3
"""Test actual FieldMetadata text fallback against fixed proxy and DEX declarations."""
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
cpp=root/'artdcevm/src/main/cpp'
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/proxy-field-text';out.mkdir(parents=True,exist_ok=True)
command=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/proxy_field_text_test.cc'),str(cpp/'platform/common/modern_field_metadata.cc'),str(cpp/'platform/common/method_identity_calibration.cc'),str(cpp/'evolution/dex_schema.cc'),'-Wl,-dead_strip' if platform=='darwin' else '-Wl,--gc-sections','-o',str(out/'test')]
if platform=='darwin':command+=['-Wl,-pagezero_size,0x10000']
subprocess.run(command,check=True)
r=subprocess.run([str(out/'test')],capture_output=True,text=True);(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
