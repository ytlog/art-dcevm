#!/usr/bin/env python3
"""Verify native Offset/MemberOffset return width against fixed ART source."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--arm64-cxx',required=True)
p.add_argument('--source-dir',type=Path,default=Path('build/evidence/compatibility/20261008/jni-offset-source'))
a=p.parse_args();root=Path(__file__).resolve().parents[1]
out=root/'build/evidence/host/jni-offset-arm64';out.mkdir(parents=True,exist_ok=True)
report=[]
for r in json.loads((a.source_dir/'manifest.json').read_text()):
    assert r['url']==f"https://android.googlesource.com/platform/art/+/{r['commit']}/{r['path']}?format=TEXT"
    data=(a.source_dir/r['tag']/r['path']).read_bytes();assert hashlib.sha256(data).hexdigest()==r['sha256']
    source=re.sub(r'^#include.*$','',data.decode(),flags=re.M)
    version=int(r['tag'].split('-')[1].split('.')[0]);kind='Offset' if version<13 else 'MemberOffset'
    assert 'size_t val_;' in source
    unit=out/(r['tag']+'.cc');ir=unit.with_suffix('.ll')
    unit.write_text('''#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <type_traits>
#define HIDDEN
namespace art {enum class PointerSize : size_t {k32=4,k64=8};}
'''+source+'''
#include "platform/abi/arm64/android11/native_offset.h"
static_assert(sizeof(art::'''+kind+''')==8&&alignof(art::'''+kind+''')==8);
static_assert(std::is_trivially_copyable_v<art::'''+kind+'''>);
static_assert(art::'''+kind+'''(0x100000008ULL).SizeValue()==0x100000008ULL);
extern "C" art::'''+kind+''' SourceOffset(size_t value) {return art::'''+kind+'''(value);}
extern "C" artdcevm::abi::arm64::android11::NativeOffset BridgeOffset(size_t value) {return {value};}
''')
    command=[a.arm64_cxx,'--target=aarch64-linux-android30','-std=c++17','-Wall','-Wextra','-Werror','-Wno-return-type-c-linkage','-O2','-S','-emit-llvm','-I',str(root/'artdcevm/src/main/cpp'),str(unit),'-o',str(ir)]
    result=subprocess.run(command,capture_output=True,text=True);unit.with_suffix('.log').write_text(result.stdout+result.stderr);result.check_returncode()
    text=ir.read_text()
    for name in ('SourceOffset','BridgeOffset'):
        body=re.search(r'define[^\n]*@'+name+r'\(.*?\n}',text,re.S).group()
        assert re.search(r'define[^\n]*i64 @'+name,body) and 'sret(' not in body and 'byval(' not in body
        assert 'ret i64 %0' in body
    report.append({'tag':r['tag'],'source_return':kind,'native_word_bytes':8,'upper_word_preserved':True,'command':command})
(out/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
print('JNI native offset literal source ABI: eight versions passed')
