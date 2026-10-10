#!/usr/bin/env python3
"""Exercise production capability routing and independent original DEX oracle."""
import argparse, os, pathlib, subprocess, sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/"tests/host"))
from schema_run_state_dex_fixture import write_fixture
root=pathlib.Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
jdk=pathlib.Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
out=root/'build/evidence/host/production-bootstrap';out.mkdir(parents=True,exist_ok=True)
common=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform)]
body_fixture=write_fixture(out/'body-routing-dex',True)
cases=[('body_routing_snapshot',[root/'tests/host/body_routing_snapshot_test.cc',cpp/'platform/common/capability_backend.cc',cpp/'platform/common/schema_snapshot.cc',cpp/'evolution/dex_schema.cc'],[str(body_fixture)]),('fresh_kept_field_ids',[root/'tests/host/fresh_kept_field_ids_test.cc'],[]),('production_provenance',[root/'tests/host/production_provenance_test.cc'],[]),('owned_method_table_provenance',[root/'tests/host/owned_method_table_provenance_test.cc'],[]),('capability_backend',[root/'tests/host/capability_backend_test.cc',cpp/'platform/common/capability_backend.cc',cpp/'platform/common/schema_snapshot.cc',cpp/'evolution/dex_schema.cc'],[]),('bootstrap_dex_oracle',[root/'tests/host/bootstrap_dex_oracle_test.cc',cpp/'platform/common/bootstrap_dex_oracle.cc',cpp/'evolution/dex_schema.cc'],[str(root/'artdcevm/build/generated/bootstrapAssets/artdcevm/bootstrap/original.dex'),str(root/'artdcevm/build/generated/bootstrapAssets/artdcevm/bootstrap/cache.dex')])]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--case',choices=[case[0] for case in cases])
parser.add_argument('--sanitizer',choices=['address','thread'],default='address')
options=parser.parse_args();selected=options.case
if options.sanitizer=='thread':common[common.index('-fsanitize=address,undefined')]='-fsanitize=thread'
for name,sources,args in cases:
 if selected and name!=selected:continue
 binary=out/(name+('-tsan' if options.sanitizer=='thread' else ''));subprocess.run(common+list(map(str,sources))+['-o',str(binary)],check=True)
 result=subprocess.run([str(binary)]+args,capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1',TSAN_OPTIONS='halt_on_error=1'));(out/(binary.name+'.log')).write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
