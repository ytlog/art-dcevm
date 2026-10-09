#!/usr/bin/env python3
"""Actual Stage/CheckWindow closure with public JNI/JVMTI boundary doubles.
This verifies the production algorithm, not device readiness.
"""
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/native-schema-stage-closure';out.mkdir(parents=True,exist_ok=True)
command=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/native_schema_stage_closure_test.cc'),'-Wl,-dead_strip' if platform=='darwin' else '-Wl,--gc-sections','-o',str(out/'test')]
command += [str(root/'artdcevm/src/main/cpp/platform/common'/(unit+'.cc')) for unit in ['modern_field_metadata','method_identity_calibration','native_interface_graph','native_schema_policy','native_method_evolution_policy','native_abstract_instances','native_schema_binding','native_method_table']]
command += [str(root/'artdcevm/src/main/cpp/evolution/dex_schema.cc')]
subprocess.run(command,check=True)
r=subprocess.run([str(out/'test')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
