#!/usr/bin/env python3
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
java=Path(subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
out=root/'build/evidence/host/native-receiver-field-entries';out.mkdir(parents=True,exist_ok=True)
binary=out/'guards_test'
(out/'native_receiver_field_fixture.inc').write_text((root/'tests/host/modern_field_var_handle_guards_test.cc').read_text().split('int main(int argc')[0])
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),'-I',str(out),'-I',str(java/'include'),'-I',str(java/'include/darwin'),str(root/'tests/host/native_receiver_field_entries_test.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_receiver_guard.cc'),str(root/'artdcevm/src/main/cpp/platform/common/retired_field_registry.cc'),str(root/'artdcevm/src/main/cpp/platform/common/modern_final_field_guard.cc'),str(root/'artdcevm/src/main/cpp/platform/common/modern_field_metadata.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_interface_graph.cc'),str(root/'artdcevm/src/main/cpp/evolution/dex_schema.cc'),'-o',str(binary)],check=True)
r=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
