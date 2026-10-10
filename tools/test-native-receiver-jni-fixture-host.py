#!/usr/bin/env python3
"""Real registrar/callback lifecycle against independent public JNI boundary."""
import os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp';jdk=pathlib.Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip());platform='darwin' if os.uname().sysname=='Darwin' else 'linux';out=root/'build/evidence/host/native-receiver-jni-fixture';out.mkdir(parents=True,exist_ok=True)
cmd=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/native_receiver_jni_fixture_test.cc'),str(cpp/'platform/common/native_receiver_jni_fixture.cc'),'-o',str(out/'test')];subprocess.run(cmd,check=True)
r=subprocess.run([str(out/'test')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
