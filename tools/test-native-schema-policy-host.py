#!/usr/bin/env python3
"""Check native schema policy and the actual prepared field-copy commit."""
import argparse, os, subprocess
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--syntax-only',action='store_true');parser.add_argument('--compile-only',action='store_true');parser.add_argument('--method-policy-root',type=Path);args=parser.parse_args()
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
out=root/'build/evidence/host/native-schema-policy';out.mkdir(parents=True,exist_ok=True)
for name,sources in [('native_schema_policy', ['platform/common/native_schema_policy.cc','platform/common/native_method_evolution_policy.cc','evolution/migration_plan.cc','evolution/dex_schema.cc']),('prepared_field_copy',['platform/common/prepared_field_copy.cc'])]:
 command=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp)]
 if name=='native_schema_policy':
  jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip());command+=['-I',str(jdk/'include'),'-I',str(jdk/'include'/('darwin' if os.uname().sysname=='Darwin' else 'linux'))]
 command += ['-I',str(args.method_policy_root)] if args.method_policy_root else []
 command += [str(root/'tests/host'/(name+'_test.cc'))]+[str((args.method_policy_root if args.method_policy_root and s=='platform/common/native_method_evolution_policy.cc' else cpp)/s) for s in sources]
 binary=out/name
 command+=['-fsyntax-only'] if args.syntax_only else ['-o',str(binary)]
 r=subprocess.run(command,capture_output=True,text=True);(out/(name+'-compile.log')).write_text(r.stdout+r.stderr);r.check_returncode()
 if not args.syntax_only and not args.compile_only:
  r=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/(name+'-result.log')).write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
print('Native schema and prepared field copy: '+('syntax PASS' if args.syntax_only else 'link PASS; execution pending' if args.compile_only else 'ASan/UBSan PASS'))
