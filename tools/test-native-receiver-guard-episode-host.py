#!/usr/bin/env python3
"""Actual Guard episode-driver regressions; ART identity leaves remain explicit."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parents[1]
out=root/'build/evidence/host/native-receiver-guard-episode';out.mkdir(parents=True,exist_ok=True)
cpp=root/'artdcevm/src/main/cpp'
units=[root/'tests/host/native_receiver_guard_episode_test.cc']+[cpp/'platform/common'/(n+'.cc') for n in ('native_receiver_guard_episode','native_receiver_guard','native_receiver_frame','native_receiver_bytecode')]
cmd=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp)]+list(map(str,units))+['-o',str(out/'test')]
r=subprocess.run(cmd,text=True,capture_output=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(out/'test')],text=True,capture_output=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
java=root/'artdcevm/src/bootstrap/receiver/com/github/ytlog/artdcevm/internal/ReceiverGuardFixture.java'
r=subprocess.run(['javac','--release','11','-d',str(out/'java'),str(java)],text=True,capture_output=True);(out/'javac.log').write_text(r.stdout+r.stderr);r.check_returncode()
