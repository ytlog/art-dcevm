#!/usr/bin/env python3
"""Actual whole scanner/source stop/typed exact Plan recovery authority."""
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp';out=root/'build/evidence/host/stopped-frame-recovery';out.mkdir(parents=True,exist_ok=True)
java=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip());platform='darwin' if os.uname().sysname=='Darwin' else 'linux';exe=out/'recovery'
args=[os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-fno-rtti','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(java/'include'),'-I',str(java/'include'/platform)]
units=[root/'tests/host/stopped_frame_recovery_proof_test.cc']+[cpp/'platform/common'/(n+'.cc') for n in ('whole_process_frames','native_source_stop','native_source_class_loading','native_cache_service','method_identity_calibration')]
subprocess.run(args+list(map(str,units))+['-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
