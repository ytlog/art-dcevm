#!/usr/bin/env python3
"""Receiver policy regression with explicit native identity/relation leaves."""
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];out=root/'build/evidence/host/native-receiver-guard';out.mkdir(parents=True,exist_ok=True)
cmd=[os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root/'artdcevm/src/main/cpp'),str(root/'tests/host/native_receiver_guard_test.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_receiver_guard.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_receiver_frame.cc'),str(root/'artdcevm/src/main/cpp/platform/common/native_receiver_bytecode.cc'),'-o',str(out/'test')]
r=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(out/'test')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())

relation_cmd=cmd.copy()
relation_cmd[relation_cmd.index(str(root/'tests/host/native_receiver_guard_test.cc'))]=str(root/'tests/host/native_receiver_relations_test.cc')
relation_cmd[relation_cmd.index(str(root/'artdcevm/src/main/cpp/platform/common/native_receiver_guard.cc'))]=str(root/'artdcevm/src/main/cpp/platform/common/native_receiver_relations.cc')
r=subprocess.run(relation_cmd,capture_output=True,text=True);(out/'relations-compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
r=subprocess.run([str(out/'test')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'relations-result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
