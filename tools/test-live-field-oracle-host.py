#!/usr/bin/env python3
"""Compile the actual production live-oracle callback with typed ART boundaries."""
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];out=root/'build/evidence/host/live-field-oracle';out.mkdir(parents=True,exist_ok=True)
s=(root/'artdcevm/src/main/cpp/platform/common/production_bootstrap.cc').read_text();s=s[s.index('bool NativeEngine::PrepareLiveFieldOracles('):];s=s[s.index('  struct Decode {'):s.index('  } decode{')]+ '  };\n'
(out/'production_callback.inc').write_text(s)
java=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
cmd=['clang++','-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(out),'-I',str(root/'artdcevm/src/main/cpp'),'-I',str(java/'include'),'-I',str(java/'include'/('darwin' if os.uname().sysname=='Darwin' else 'linux')),str(root/'tests/host/live_field_oracle_test.cc'),'-o',str(out/'test')]
p=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(p.stdout+p.stderr);p.check_returncode();p=subprocess.run([str(out/'test')],capture_output=True,text=True);(out/'result.log').write_text(p.stdout+p.stderr);p.check_returncode();print(p.stdout.strip())
