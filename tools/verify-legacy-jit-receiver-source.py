#!/usr/bin/env python3
"""Check fixed official 11/12 bool/counter JIT ABI with the ARM64 NDK."""
import argparse,base64,hashlib,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--arm64-cxx',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=root/'build/evidence/host/legacy-jit-source';out.mkdir(parents=True,exist_ok=True)
commits=('34f62af4d6425087667b7eb3998fcf58d61a2fa6','8d44c3d61d4bde412f06190cd4acc4afc2ea469c')
manifest=[]
for commit in commits:
 url=f'https://android.googlesource.com/platform/art/+/{commit}/runtime/jit/jit.h?format=TEXT';source=out/(commit+'.h')
 if not source.exists():source.write_bytes(base64.b64decode(subprocess.run(['curl','-fsSL','--max-time','45',url],check=True,capture_output=True).stdout))
 text=source.read_text();decl=re.search(r'bool MaybeCompileMethod\(Thread\* self,\s*ArtMethod\* method,\s*uint32_t old_count,\s*uint32_t new_count,\s*bool with_backedges\)',text)
 assert decl,'Exact fixed legacy JIT declaration changed'
 unit=out/(commit+'.cc');ir=out/(commit+'.ll')
 unit.write_text('#include <cstdint>\nnamespace art {class Thread;class ArtMethod;namespace jit {class Jit {public:\n'+decl.group()+';};\nbool Jit::MaybeCompileMethod(Thread*,ArtMethod*,uint32_t,uint32_t,bool){return true;}\n}}\n')
 command=[a.arm64_cxx,'-std=c++17','-S','-emit-llvm','-O1',str(unit),'-o',str(ir)]
 subprocess.run(command,check=True,capture_output=True)
 symbol='_ZN3art3jit3Jit18MaybeCompileMethodEPNS_6ThreadEPNS_9ArtMethodEjjb';llvm=ir.read_text()
 assert re.search(r'define[^\n]*i1 @'+symbol+r'\(ptr[^,]*, ptr[^,]*, ptr[^,]*, i32[^,]*, i32[^,]*, i1',llvm)
 assert '"'+symbol+'"' in (root/'artdcevm/src/main/cpp/platform/common/owned_jit_receivers.cc').read_text()
 manifest.append({'commit':commit,'url':url,'sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'symbol':symbol,'boolReturn':True,'countersBits':32})
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print('Legacy JIT exact 11/12 declarations: ARM64 bool return, full uint32 counters, bool argument and named symbol passed')
