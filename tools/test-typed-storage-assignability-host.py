#!/usr/bin/env python3
"""Test the production typed-storage relation without granting heap authority."""
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
out=root/'build/evidence/host/typed-storage-assignability';out.mkdir(parents=True,exist_ok=True)
binary=out/'typed_storage_assignability_test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-Werror','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),str(root/'tests/host/typed_storage_assignability_test.cc'),'-o',str(binary)],check=True)
result=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
