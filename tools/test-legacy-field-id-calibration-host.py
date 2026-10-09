#!/usr/bin/env python3
"""Real legacy runtime TU with adversarial JNI/source boundaries; no device claim."""
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp';java=Path(subprocess.check_output(['/usr/libexec/java_home'],text=True).strip());out=root/'build/evidence/host/legacy-field-id-calibration';out.mkdir(parents=True,exist_ok=True);binary=out/'probe'
subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g','-O1','-I'+str(cpp),'-I'+str(root/'third_party/jvmti'),'-I'+str(java/'include'),'-I'+str(java/'include/darwin'),str(root/'tests/host/legacy_field_id_calibration_test.cc'),str(cpp/'platform/android13/runtime_access.cc'),str(cpp/'platform/android13/device_profile.cc'),'-o',str(binary)],check=True)
for mode in range(14):
 r=subprocess.run([str(binary),str(mode)],capture_output=True,text=True);(out/('mode-'+str(mode)+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
