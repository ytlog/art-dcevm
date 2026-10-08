#!/usr/bin/env python3
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
java=Path(subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
out=root/'build/evidence/host/contextual-field-id';out.mkdir(parents=True,exist_ok=True)
for sanitizer in ('address,undefined','thread'):
 binary=out/('contextual_'+sanitizer.replace(',','-'))
 subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-pthread','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize='+sanitizer,'-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),'-I',str(java/'include'),'-I',str(java/'include/darwin'),str(root/'tests/host/contextual_field_id_test.cc'),str(root/'artdcevm/src/main/cpp/platform/common/retired_field_registry.cc'),str(root/'artdcevm/src/main/cpp/platform/common/retired_method_registry.cc'),'-o',str(binary)],check=True)
 r=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1',TSAN_OPTIONS='halt_on_error=1'));(out/(sanitizer.replace(',','-')+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();print(sanitizer+': '+r.stdout.strip())
