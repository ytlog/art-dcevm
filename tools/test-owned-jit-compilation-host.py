#!/usr/bin/env python3
import os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[1];out=root/'build/evidence/host/owned-jit-compilation';out.mkdir(parents=True,exist_ok=True)
binary=out/'test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),str(root/'tests/host/owned_jit_compilation_test.cc'),'-o',str(binary)],check=True)
result=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'result.log').write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
