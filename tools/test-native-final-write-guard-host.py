#!/usr/bin/env python3
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
out=root/'build/evidence/host/native-final-write-guard';out.mkdir(parents=True,exist_ok=True)
binary=out/'guard_test'
sources=['tests/host/native_final_write_guard_test.cc','artdcevm/src/main/cpp/platform/common/native_final_write_guard.cc','artdcevm/src/main/cpp/platform/common/modern_final_field_guard.cc']
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-I',str(root/'artdcevm/src/main/cpp'),*[str(root/s) for s in sources],'-o',str(binary)],check=True)
r=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())

listener=out/'listener_test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-DARTDCEVM_TESTING','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-pthread','-I',str(root/'artdcevm/src/main/cpp'),str(root/'tests/host/native_final_write_listener_test.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_final_write_guard.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_final_write_listener.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_receiver_guard.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_receiver_frame.cc'),str(root/'artdcevm/src/main/cpp/platform/common/modern_final_field_guard.cc'),'-o',str(listener)],check=True)
for family in ('3','4','5','6'):
 r=subprocess.run([str(listener),family],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
 (out/('listener-'+family+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
