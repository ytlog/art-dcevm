#!/usr/bin/env python3
"""Compile domain capability and each changed owner with actual ARM64 NDK flags."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
parser=argparse.ArgumentParser()
parser.add_argument('--commands',type=Path,required=True)
arguments=parser.parse_args()
root=Path(__file__).resolve().parents[1]
suffix='/artdcevm/src/main/cpp/platform/common/native_schema_run_state.cc'
entry=next(item for item in json.loads(arguments.commands.read_text()) if item['file'].endswith(suffix))
old_root=entry['file'].removesuffix(suffix)
command=[part.replace(old_root,str(root)) for part in shlex.split(entry['command'])]
out=root/'build/evidence/host/native-interface-dispatch-domain/arm64'
out.mkdir(parents=True,exist_ok=True)
sources=['native_interface_dispatch_domain','native_interface_dispatch','native_hierarchy_graph','native_schema_guard_verifier','native_schema_run_state']
for name in sources:
    source=root/'artdcevm/src/main/cpp/platform/common'/(name+'.cc')
    invocation=command[:]
    invocation[invocation.index('-c')+1]=str(source)
    invocation[invocation.index('-o')+1]=str(out/(name+'.o'))
    (out/(name+'-command.json')).write_text(json.dumps(invocation,indent=2)+'\n')
    result=subprocess.run(invocation,cwd=root,capture_output=True,text=True)
    (out/(name+'.log')).write_text(result.stdout+result.stderr)
    result.check_returncode()
    print(name,'actual ARM64 TU PASS')
