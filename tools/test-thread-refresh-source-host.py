#!/usr/bin/env python3
"""Check source metadata lookup, invalid enum/output preservation, and native ABI proof."""
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
out=root/'build/evidence/host/thread-refresh-source'
out.mkdir(parents=True,exist_ok=True)
binary=out/'thread_refresh_source_test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-I',str(root/'artdcevm/src/main/cpp'),str(root/'tests/host/thread_refresh_source_test.cc'),'-o',str(binary)],check=True)
result=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'host-test.log').write_text(result.stdout+result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('thread refresh source metadata: ASan/UBSan passed')
