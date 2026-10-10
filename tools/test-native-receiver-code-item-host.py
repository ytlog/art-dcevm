#!/usr/bin/env python3
import os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[1];out=root/'build/evidence/host/native-receiver-code-item';out.mkdir(parents=True,exist_ok=True)
cmd=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(root/'artdcevm/src/main/cpp'),str(root/'tests/host/native_receiver_code_item_test.cc'),'-o',str(out/'test')];r=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode();r=subprocess.run([str(out/'test')],capture_output=True,text=True);(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
