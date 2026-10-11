#!/usr/bin/env python3
"""Validate real production private lifecycle DEX, independent of app fixtures."""
import os,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
out=root/'build/evidence/host/ordinary-source-fixture';out.mkdir(parents=True,exist_ok=True)
binary=out/'ordinary-source-fixture'
command=[os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),str(root/'tests/host/ordinary_source_fixture_test.cc'),str(cpp/'platform/common/ordinary_source_fixture.cc'),str(cpp/'evolution/dex_schema.cc'),'-o',str(binary)]
subprocess.run(command,check=True)
assets=root/'artdcevm/build/generated/bootstrapAssets/artdcevm/bootstrap'
result=subprocess.run([str(binary)]+[str(assets/(v+'.dex')) for v in ('source-second','source-first','source-original')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'result.log').write_text(result.stdout+result.stderr);result.check_returncode();print(result.stdout.strip())
