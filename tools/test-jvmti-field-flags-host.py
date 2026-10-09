#!/usr/bin/env python3
"""Actual FinalService TU flags regression; host doubles are not device proof."""
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/jvmti-field-flags';out.mkdir(parents=True,exist_ok=True)
cmd=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/jvmti_field_flags_test.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_schema_policy.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_removal_policy.cc'),'-Wl,-dead_strip' if platform=='darwin' else '-Wl,--gc-sections','-o',str(out/'test')]
r=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(out/'test')],capture_output=True,text=True);(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
