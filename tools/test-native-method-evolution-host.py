#!/usr/bin/env python3
"""Actual policy/abstract-vtable services; --syntax-only supports restricted hosts."""
import argparse, os, subprocess
from pathlib import Path
parser=argparse.ArgumentParser();mode=parser.add_mutually_exclusive_group();mode.add_argument('--syntax-only',action='store_true');mode.add_argument('--build-only',action='store_true');args=parser.parse_args()
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
out=root/'build/evidence/host/native-method-evolution';out.mkdir(parents=True,exist_ok=True)
fixture=(root/'tests/host/native_source_commit_test.cc').read_text()
(out/'native_commit_fixture.inc').write_text(fixture.split('int main(){')[0])
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
flags=['-std=c++17','-Wall','-Wextra','-Werror','-I',str(cpp),'-I',str(out),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform)]
sources={'policy':[root/'tests/host/native_method_evolution_policy_test.cc',cpp/'platform/common/native_method_evolution_policy.cc',cpp/'evolution/dex_schema.cc'],
 'abstract':[root/'tests/host/native_abstract_instances_test.cc',cpp/'platform/common/native_abstract_instances.cc',cpp/'platform/common/modern_field_metadata.cc',cpp/'platform/common/method_identity_calibration.cc',cpp/'platform/common/native_interface_graph.cc',cpp/'evolution/dex_schema.cc']}
logs=[]
for name,units in sources.items():
 compiler=os.environ.get('CXX','clang++')
 if args.syntax_only:
  result=subprocess.run([compiler,*flags,'-fsyntax-only',*map(str,units)],capture_output=True,text=True)
  logs.append(name+': '+result.stdout+result.stderr);(out/'syntax.log').write_text(''.join(logs));result.check_returncode();print(name+': production TU and C++ cases syntax PASS')
 else:
  binary=out/name
  command=[compiler,*flags,'-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',*map(str,units),'-o',str(binary)]
  if platform=='darwin' and 'android' not in Path(compiler).name:command+=['-Wl,-pagezero_size,0x10000']
  subprocess.run(command,check=True)
  if args.build_only:
   print(name+': test binary built (not executed): '+str(binary));continue
  result=subprocess.run([str(binary)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
  logs.append(name+': '+result.stdout+result.stderr);(out/'result.log').write_text(''.join(logs));result.check_returncode();print(result.stdout.strip())

if not args.syntax_only and not args.build_only:
 java_out=out/'java';java_out.mkdir(exist_ok=True)
 subprocess.run([str(jdk/'bin/javac'),'-d',str(java_out),str(root/'artdcevm/src/main/java/com/github/ytlog/artdcevm/internal/AbstractVTableOracle.java'),str(root/'tests/host/AbstractVTableOracleTest.java')],check=True)
 result=subprocess.run([str(jdk/'bin/java'),'-cp',str(java_out),'AbstractVTableOracleTest'],capture_output=True,text=True)
 (out/'java-original.log').write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
