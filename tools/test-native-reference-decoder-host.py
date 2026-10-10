#!/usr/bin/env python3
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];java=Path(subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
out=root/'build/evidence/host/native-reference-decoder';out.mkdir(parents=True,exist_ok=True)
binary=out/'decoder'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-Werror','-g','-O1','-fsanitize=address,undefined','-pthread','-I',str(root/'artdcevm/src/main/cpp'),'-I',str(java/'include'),'-I',str(java/'include/darwin'),str(root/'tests/host/native_reference_decoder_test.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_reference_decoder.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_receiver_frame.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_receiver_bytecode.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_receiver_source_calibration.cc'),'-o',str(binary)],check=True)
for mode in range(12):
 r=subprocess.run([str(binary),str(mode)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/('mode-'+str(mode)+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
