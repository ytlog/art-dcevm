#!/usr/bin/env python3
"""Compile actual super adapter and bound source mechanics; no fake source owner."""
import os,subprocess,json,hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp';out=root/'build/evidence/host/native-call-graph-super';out.mkdir(parents=True,exist_ok=True)
java=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip());platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
flags=[os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-Werror','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(java/'include'),'-I',str(java/'include'/platform)]
paths=['artdcevm/src/main/cpp/platform/common/native_call_graph_super.h','artdcevm/src/main/cpp/platform/common/native_call_graph_super.cc','tests/host/native_call_graph_super_test.cc','tools/test-native-call-graph-super-host.py']
with (out/'test.log').open('w') as log:
 subprocess.run(flags+['-c',str(root/paths[1]),'-o',str(out/'adapter.o')],stdout=log,stderr=subprocess.STDOUT,check=True)
 subprocess.run(flags+[str(root/paths[2]),str(cpp/'platform/common/method_identity_calibration.cc'),'-o',str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True)
 subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True)
(out/'receipt.json').write_text(json.dumps({'scope':'actual adapter TU compile, fresh OS mapping recapture after changed access, bounded source method/vtable mechanics and original ABI forwarding; full rooted inventory/real resolver integration pending','sourceHashes':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths}},indent=2)+'\n')
print('Super adapter production TU and bounded source ABI ASan/UBSan PASS (full integration pending)')
