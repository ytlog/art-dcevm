#!/usr/bin/env python3
"""Bounded source regression; does not simulate exclusive/Instrumentation owners."""
import os,subprocess,json,hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp';out=root/'build/evidence/host/native-selective-interpretation';out.mkdir(parents=True,exist_ok=True)
java=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
flags=[os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-Werror','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(java/'include'),'-I',str(java/'include'/platform)]
source=cpp/'platform/common/native_selective_interpretation.cc'
with (out/'test.log').open('w') as log:
 subprocess.run(flags+['-c',str(source),'-o',str(out/'service.o')],stdout=log,stderr=subprocess.STDOUT,check=True)
 subprocess.run(flags+[str(root/'tests/host/native_selective_interpretation_test.cc'),'-o',str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True)
 subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True)
paths=['artdcevm/src/main/cpp/platform/common/native_selective_interpretation.h','artdcevm/src/main/cpp/platform/common/native_selective_interpretation.cc','tests/host/native_selective_interpretation_test.cc','tools/test-native-selective-interpretation-host.py']
(out/'receipt.json').write_text(json.dumps({'scope':'actual production TU compile and bounded fixed-source predicate regression; activation/real owner integration not claimed','sourceHashes':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths}},indent=2)+'\n')
print('Selective interpretation source regression ASan/UBSan and production TU compile PASS (activation integration pending)')
