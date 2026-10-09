#!/usr/bin/env python3
"""Actual service refresh TU; explicit prepared roots and operation-gate fixtures."""
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
cpp=root/'artdcevm/src/main/cpp'
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/permission-plan-refresh';out.mkdir(parents=True,exist_ok=True)
command=[os.environ.get('CXX','clang++'),'-std=c++17','-DARTDCEVM_TESTING','-g','-O1','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/native_permission_plan_refresh_test.cc')]
for name in ['native_final_write_guard','native_final_write_listener','modern_final_field_guard','native_final_field_aliases','modern_field_metadata','method_identity_calibration','native_schema_policy']:
 command.append(str(cpp/'platform/common'/(name+'.cc')))
command.append(str(cpp/'evolution/dex_schema.cc'))
command.extend(['-Wl,-dead_strip' if platform=='darwin' else '-Wl,--gc-sections','-o',str(out/'test')])
subprocess.run(command,check=True)
r=subprocess.run([str(out/'test')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
