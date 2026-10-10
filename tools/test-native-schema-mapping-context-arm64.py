#!/usr/bin/env python3
"""Compile mapping-context production TUs using configured NDK settings."""
import argparse,json,shlex,subprocess
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--commands',type=Path,required=True);parser.add_argument('--provider',action='store_true',help='also compile the member/provider/copy/reflection pipeline');args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
entries=json.loads(args.commands.read_text())
out=root/'build/evidence/host/schema-mapping-context-arm64';out.mkdir(parents=True,exist_ok=True)
units=['modern_field_metadata','native_schema_guard_verifier']
if args.provider:units+=['field_copy','prepared_field_copy','modern_field_storage','modern_method_services','native_member_provider','native_method_table','native_schema_binding','native_source_commit', 'native_initialized_transfer','native_structural_reflection','modern_source_commit']
for unit in units:
    suffix='/artdcevm/src/main/cpp/platform/common/'+unit+'.cc'
    entry=next(e for e in entries if e['file'].endswith(suffix))
    original=entry['file'].removesuffix(suffix)
    command=[v.replace(original,str(root)) for v in shlex.split(entry['command'])]
    command[command.index('-o')+1]=str(out/(unit+'.o'))
    command.extend(['-Wall','-Wextra','-Werror'])
    result=subprocess.run(command,cwd=root,capture_output=True,text=True)
    (out/(unit+'.log')).write_text(result.stdout+result.stderr)
    result.check_returncode();print(unit+' NDK ARM64 strict PASS')
