#!/usr/bin/env python3
"""Verify source commit ordering and pre-mutation transaction rejection."""
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
cpp=root/'artdcevm/src/main/cpp'
out=root/'build/evidence/host/source-commit';out.mkdir(parents=True,exist_ok=True)
binary=out/'source_commit_test'
command=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),str(root/'tests/host/source_commit_test.cc'),str(cpp/'platform/common/source_commit.cc'),'-o',str(binary)]
subprocess.run(command,check=True)
r=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'test.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
