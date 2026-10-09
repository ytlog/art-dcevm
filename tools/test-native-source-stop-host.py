#!/usr/bin/env python3
"""Compile the actual ARM64 source-stop owner TU and validate original scope transfer."""
import os,signal,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/native-source-stop';out.mkdir(parents=True,exist_ok=True);binary=out/'native_source_stop_test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/native_source_stop_test.cc'),str(cpp/'platform/common/native_source_stop.cc'),'-o',str(binary)],check=True)
for mode in ('',*[f'install-rollback{n}' for n in range(1,7)],'bad-ctor-alias','bad-dtor-alias','duplicate-target','missing-heap-gc','wrong-final'):
 r=subprocess.run([str(binary)]+([mode] if mode else []),capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
 (out/(mode or 'ownership')).with_suffix('.log').write_text(r.stdout+r.stderr)
 if mode=='wrong-final':
  assert r.returncode==-signal.SIGABRT,(r.returncode,r.stdout,r.stderr)
 else:r.check_returncode()
 print(mode or 'ownership','PASS')

scanner=out/'whole_process_frames_test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/whole_process_frames_test.cc'),str(cpp/'platform/common/native_source_stop.cc'),str(cpp/'platform/common/native_cache_service.cc'),str(cpp/'platform/common/method_identity_calibration.cc'),'-o',str(scanner)],check=True)
r=subprocess.run([str(scanner)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'whole-scanner.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
