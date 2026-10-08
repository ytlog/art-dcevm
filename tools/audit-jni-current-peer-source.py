#!/usr/bin/env python3
"""Verify current-thread and peer source ABIs and captured named entry models."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--arm64-cxx',required=True)
p.add_argument('--source-dir',type=Path,default=Path('build/evidence/compatibility/20261008/jni-current-peer-source'))
p.add_argument('--device-dir',type=Path,default=Path('build/evidence/compatibility/20261008'))
a=p.parse_args();root=Path(__file__).resolve().parents[1]
out=root/'build/evidence/host/jni-current-peer-arm64';out.mkdir(parents=True,exist_ok=True)
sources={}
for r in json.loads((a.source_dir/'manifest.json').read_text()):
    assert r['url']==f"https://android.googlesource.com/platform/art/+/{r['commit']}/{r['path']}?format=TEXT"
    data=(a.source_dir/r['tag']/r['path']).read_bytes();assert hashlib.sha256(data).hexdigest()==r['sha256']
    sources[r['tag'],r['path']]=data.decode()
report={'fixed_source':[],'devices':[]}
for tag in sorted({t for t,_ in sources}):
    header=sources[tag,'runtime/thread.h'];cc=sources[tag,'runtime/thread.cc']
    current=re.search(r'static Thread\* CurrentFromGdb\(\);',header).group()
    peer=re.search(r'mirror::Object\* GetPeerFromOtherThread\(\)( const)?',header).group()+';'
    assert re.search(r'Thread\* Thread::CurrentFromGdb\(\)\s*{\s*return Thread::Current\(\);\s*}',cc)
    is_const=' const' in peer
    assert is_const==(int(tag.split('-')[1].split('.')[0])<15)
    family='android11' if is_const else 'android15'
    unit=out/(tag+'.cc');ir=unit.with_suffix('.ll')
    unit.write_text('''#include "platform/abi/arm64/'''+family+'''/jni_current_thread.h"
namespace art {namespace mirror {class Object;} class Thread {public:
'''+current+peer+'''
};}
extern "C" void* SourceCurrent() {return art::Thread::CurrentFromGdb();}
extern "C" void* BridgeCurrent(artdcevm::abi::arm64::'''+family+'''::CurrentThread fn) {return fn();}
extern "C" void* SourcePeer('''+('const ' if is_const else '')+'''art::Thread* self) {return self->GetPeerFromOtherThread();}
extern "C" void* BridgePeer(artdcevm::abi::arm64::'''+family+'''::CurrentPeer fn,'''+('const ' if is_const else '')+'''void* self) {return fn(self);}
''')
    command=[a.arm64_cxx,'--target=aarch64-linux-android30','-std=c++17','-Wall','-Wextra','-Werror','-O2','-S','-emit-llvm','-I',str(root/'artdcevm/src/main/cpp'),str(unit),'-o',str(ir)]
    r=subprocess.run(command,capture_output=True,text=True);unit.with_suffix('.log').write_text(r.stdout+r.stderr);r.check_returncode();text=ir.read_text()
    symbol=('_ZNK' if is_const else '_ZN')+'3art6Thread22GetPeerFromOtherThreadEv'
    assert symbol in text and '_ZN3art6Thread14CurrentFromGdbEv' in text
    for name in ('SourceCurrent','BridgeCurrent','SourcePeer','BridgePeer'):
        body=re.search(r'define[^\n]*@'+name+r'\(.*?\n}',text,re.S).group()
        assert re.search(r'define[^\n]*ptr @'+name,body) and 'sret(' not in body and 'byval(' not in body
    report['fixed_source'].append({'tag':tag,'peer_symbol':symbol,'current':'static direct pointer return','peer':'borrowed raw pointer return; call only on current owned thread','command':command})
for device in ('firebase-redfin-30-compressed','firebase-oriole-31','firebase-bluejay-32','firebase-panther-33','firebase-b0q-33','firebase-shiba-34','firebase-husky-35','firebase-frankel-36','firebase-cubs-37','local-collector-final'):
    file=next((a.device_dir/device/'libraries').glob('*libart.so/defined-symbols.json'))
    data=file.read_bytes();symbols=set(json.loads(data))
    assert '_ZN3art6Thread14CurrentFromGdbEv' in symbols
    peer=[s for s in ('_ZNK3art6Thread22GetPeerFromOtherThreadEv','_ZN3art6Thread22GetPeerFromOtherThreadEv') if s in symbols]
    assert len(peer)==1
    report['devices'].append({'device':device,'peer_symbol':peer[0],'inventory_sha256':hashlib.sha256(data).hexdigest()})
report['limit']='Symbol/source proof does not publish a profile; real callback and independent public currentThread oracle required. No Thread fields read.'
(out/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
print('JNI current/peer source ABI: eight declarations and ten device symbol sets passed')
