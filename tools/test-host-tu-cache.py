#!/usr/bin/env python3
"""Real clang cache regression: exact dependencies, damage and failure refusal."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
from host_tu_cache import compile_cached
root=Path(__file__).resolve().parents[1]
out=root/'build/evidence/host/tu-cache';out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(dir=out) as raw:
 d=Path(raw);objects=d/'objects'
 (d/'shared.h').write_text('#define SHARED 2\n')
 (d/'fixture.inc').write_text('#define FIXTURE 3\n')
 (d/'main.cc').write_text('#include "shared.h"\n#include "fixture.inc"\nint value();int main(){return value()+FIXTURE-SHARED-3;}\n')
 (d/'value.cc').write_text('#include "shared.h"\n#if __has_include("optional.h")\n#include "optional.h"\n#endif\nint value(){return SHARED;}\n')
 base=[os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(d)]
 units=[d/'main.cc',d/'value.cc'];events=[]
 def build(expected,flags=None):
  result=compile_cached(flags or base,units,objects)
  stats=json.loads((objects/'last-run.json').read_text());assert stats['compiled']==expected,stats
  events.append({'compiled':stats['compiled'],'reused':stats['reused']})
  return result
 cold=build(2);subprocess.run(base+cold+['-o',str(d/'test')],check=True);subprocess.run([str(d/'test')],check=True)
 assert build(0)==cold
 # Fixture input rebuilds only the TU that actually consumes it.
 (d/'fixture.inc').write_text('#define FIXTURE 3\n// new fixture content\n');build(1)
 (d/'value.cc').write_text((d/'value.cc').read_text()+'// source mutation\n');build(1)
 (d/'shared.h').write_text('#define SHARED 2\n// transitive header mutation\n');build(2)
 # Newly available conditional includes are found even without old dep entries.
 (d/'optional.h').write_text('#define OPTIONAL 1\n');build(1)
 (d/'optional.h').unlink();build(0)  # Exact earlier input set is safely reusable.
 build(2,base+['-O1']);current=build(0,base+['-O1'])
 Path(current[0]).write_bytes(b'corrupted object');build(1,base+['-O1'])
 Path(current[1]).unlink();build(1,base+['-O1'])
 build(0)
 previous=(d/'value.cc').read_text();(d/'value.cc').write_text('#error required failure\n')
 try:build(0)
 except subprocess.CalledProcessError:pass
 else:raise AssertionError('Failed compiler output was accepted')
 (d/'value.cc').write_text(previous);build(0)
 (out/'result.json').write_text(json.dumps(events,indent=2)+'\n')
 print('Real clang TU cache cold/hot, fixture/source/header/optional include/flags, corrupted/missing object, compilation failure and restored inputs PASS')
