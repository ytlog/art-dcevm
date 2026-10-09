#!/usr/bin/env python3
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
java=Path(subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
out=root/'build/evidence/host/modern-field-var-handle-guards';out.mkdir(parents=True,exist_ok=True)
binary=out/'guards_test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),'-I',str(java/'include'),'-I',str(java/'include/darwin'),str(root/'tests/host/modern_field_var_handle_guards_test.cc'),str(root/'artdcevm/src/main/cpp/platform/common/retired_field_registry.cc'),str(root/'artdcevm/src/main/cpp/platform/common/modern_field_metadata.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_interface_graph.cc'),str(root/'artdcevm/src/main/cpp/evolution/dex_schema.cc'),'-o',str(binary)],check=True)
logs=[]
for mode in ('named','prefix'):
 r=subprocess.run([str(binary),mode],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
 logs.append(mode+': '+r.stdout+r.stderr);(out/'result.log').write_text(''.join(logs));r.check_returncode();print(mode+': '+r.stdout.strip())

java_out=out/'java';java_out.mkdir(exist_ok=True)
subprocess.run([str(java/'bin/javac'),'-d',str(java_out),str(root/'artdcevm/src/main/java/com/github/ytlog/artdcevm/internal/VarHandleClassOracle.java'),str(root/'tests/host/VarHandleClassOracleTest.java')],check=True)
r=subprocess.run([str(java/'bin/java'),'-cp',str(java_out),'VarHandleClassOracleTest'],capture_output=True,text=True)
(out/'java-original-array-oracle.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
