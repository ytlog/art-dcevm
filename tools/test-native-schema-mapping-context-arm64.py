#!/usr/bin/env python3
"""Compile both mapping-context production TUs using configured NDK settings."""
import argparse,json,shlex,subprocess
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--commands',type=Path,required=True);args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
entries=json.loads(args.commands.read_text())
out=root/'build/evidence/host/schema-mapping-context-arm64';out.mkdir(parents=True,exist_ok=True)
for unit in ('modern_field_metadata','native_schema_guard_verifier'):
    suffix='/artdcevm/src/main/cpp/platform/common/'+unit+'.cc'
    entry=next(e for e in entries if e['file'].endswith(suffix))
    original=entry['file'].removesuffix(suffix)
    command=[v.replace(original,str(root)) for v in shlex.split(entry['command'])]
    command[command.index('-o')+1]=str(out/(unit+'.o'))
    command.extend(['-Wall','-Wextra','-Werror'])
    result=subprocess.run(command,cwd=root,capture_output=True,text=True)
    (out/(unit+'.log')).write_text(result.stdout+result.stderr)
    result.check_returncode();print(unit+' NDK ARM64 strict PASS')
