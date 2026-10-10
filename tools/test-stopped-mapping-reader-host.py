#!/usr/bin/env python3
"""Actual allocation-free stopped /proc mapping reader with bounded sinks."""
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
out=root/'build/evidence/host/stopped-mapping-reader'
out.mkdir(parents=True,exist_ok=True)
exe=out/'test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),str(root/'tests/host/stopped_mapping_reader_test.cc'),'-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(r.stdout+r.stderr)
r.check_returncode()
print(r.stdout.strip())
