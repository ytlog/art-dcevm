#!/usr/bin/env python3
"""Verify one real native collector Hook, subscriptions and exact forwarding."""
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
out=root/'build/evidence/host/heap-collect-observer';out.mkdir(parents=True,exist_ok=True)
binary=out/'heap_collect_observer_test'
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),str(root/'tests/host/heap_collect_observer_test.cc'),str(cpp/'platform/common/heap_collect_observer.cc'),'-o',str(binary)],check=True)
logs=[]
for mode in ('success','missing','ctor','dtor','install-failed'):
 r=subprocess.run([str(binary),mode],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
 logs.append(r.stdout+r.stderr);(out/'test.log').write_text(''.join(logs));print(r.stdout,end='');r.check_returncode()
