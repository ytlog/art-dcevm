#!/usr/bin/env python3
"""Compile new actual source-stop TU and full scanner with configured ARM64 NDK flags."""
import argparse,json,shlex,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--commands',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];cpp=root/'artdcevm/src/main/cpp'
entry=next(e for e in json.loads(a.commands.read_text()) if e['file'].endswith('/native_source_commit.cc'))
# Derive the precise repo root from the known source suffix, avoiding directory guesses.
old_root=entry['file'].removesuffix('/artdcevm/src/main/cpp/platform/common/native_source_commit.cc')
command=[v.replace(old_root,str(root)) for v in shlex.split(entry['command'])]
out=root/'build/evidence/host/native-source-stop/arm64';out.mkdir(parents=True,exist_ok=True)
wrapper=out/'whole_process_frames_compile.cc';wrapper.write_text('#include "platform/common/whole_process_frames.h"\n#include "platform/common/native_source_calibration_bridge.h"\n')
for source in [cpp/'platform/common/native_source_stop.cc',cpp/'platform/common/native_source_class_loading.cc',cpp/'platform/common/native_cache_service.cc',cpp/'platform/common/whole_process_frames.cc',cpp/'platform/common/native_required_cleanup.cc',wrapper]:
 args=command[:];args[args.index('-c')+1]=str(source);args[args.index('-o')+1]=str(out/(source.stem+'.o'))
 result=subprocess.run(args,cwd=root,capture_output=True,text=True)
 (out/(source.stem+'.log')).write_text(result.stdout+result.stderr);result.check_returncode();print(source.name,'NDK ARM64 actual TU PASS')
