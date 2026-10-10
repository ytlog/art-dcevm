#!/usr/bin/env python3
"""Exercise actual platform map capture and explicit fail-closed native view."""
import os,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp';out=root/'build/evidence/host/ordinary-mapping-snapshot';out.mkdir(parents=True,exist_ok=True)
jdk=Path(os.environ.get('JAVA_HOME') or subprocess.check_output(['/usr/libexec/java_home'],text=True).strip());platform='darwin' if os.uname().sysname=='Darwin' else 'linux'
command=[os.environ.get('CXX','clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(cpp),'-I',str(jdk/'include'),'-I',str(jdk/'include'/platform),str(root/'tests/host/ordinary_mapping_snapshot_test.cc'),str(cpp/'platform/common/method_identity_calibration.cc'),'-o',str(out/'test')]
r=subprocess.run(command,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr);r.check_returncode()
# Retain the exact device failure prefix alongside independent nonempty
# records; the test compares bounds and nonempty permissions to sscanf.
zero_fixture=out/'kernel-zero-record.maps'
zero_fixture.write_text('7b68a71000-7b68a72000 r--p 0002c000 fe:43 106\n7b68a72000-7b68a72000 rwxp 0002d000 fe:43 106\n7b68a72000-7b68a73000 rwxp 0002d000 fe:43 106\n')
fixture_run=subprocess.run([str(out/'test'),str(zero_fixture)],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
(out/'zero-record-result.log').write_text(fixture_run.stdout+fixture_run.stderr);fixture_run.check_returncode()
r=subprocess.run([str(out/'test'),*sys.argv[1:]],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));(out/'result.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
