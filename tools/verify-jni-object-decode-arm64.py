#!/usr/bin/env python3
"""Compile literal fixed ART ObjPtr and named decoder declarations on ARM64."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--arm64-cxx',required=True)
parser.add_argument('--source-dir',type=Path,default=Path('build/evidence/compatibility/20261008/native-access-source'))
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
records=[]
for name in ('manifest.json','broker-manifest.json','check-jni-manifest.json'):
    records+=json.loads((args.source_dir/name).read_text())
output=root/'build/evidence/host/jni-object-decode-arm64'
output.mkdir(parents=True,exist_ok=True)
def read(tag,path):
    record=next(r for r in records if r['tag']==tag and r['path']==path)
    assert record['url']==f"https://android.googlesource.com/platform/art/+/{record['commit']}/{path}?format=TEXT"
    data=(args.source_dir/tag/path).read_bytes()
    assert hashlib.sha256(data).hexdigest()==record['sha256']
    return data.decode()
report=[]
for tag in sorted({r['tag'] for r in records}):
    modern=int(tag.split('-')[1].split('.')[0])>=14
    name='DecodeGlobalJObject' if modern else 'DecodeJObject'
    declaration=re.search(r'ObjPtr<mirror::Object> Thread::'+name+r'\(jobject obj\) const',read(tag,'runtime/thread.cc')).group().replace('Thread::','')+';'
    obj=read(tag,'runtime/obj_ptr.h')
    assert 'uintptr_t reference_' in obj and 'kObjPtrPoisoning = kIsDebugBuild' in obj
    unit=output/(tag+'.cc')
    unit.write_text('''#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iosfwd>
#include <type_traits>
#define HIDDEN
#define EXPORT
#define ALWAYS_INLINE inline
#define REQUIRES_SHARED(...)
#define REQUIRES(...)
#define NO_THREAD_SAFETY_ANALYSIS
#define DCHECK(x) ((void)0)
namespace art {
constexpr bool kIsDebugBuild=false;
constexpr size_t kHeapReferenceSize=4,kBitsPerByte=8,kObjectAlignmentShift=3;
namespace mirror {class Object;}
}
''' + re.sub(r'^#include.*$','',obj,flags=re.M)+'''
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include "platform/abi/arm64/android14/jni_global_decode.h"
namespace art {class Thread {public:
'''+declaration+'''
};}
static_assert(sizeof(art::ObjPtr<art::mirror::Object>)==8);
static_assert(alignof(art::ObjPtr<art::mirror::Object>)==8);
static_assert(std::is_trivially_copyable_v<art::ObjPtr<art::mirror::Object>>);
static_assert(!art::kObjPtrPoisoning);
static_assert(!art::kObjPtrPoisoningValidateOnCopy);
extern "C" art::ObjPtr<art::mirror::Object> SourceDecode(const art::Thread* self,jobject obj) {
 return self->'''+name+'''(obj);
}
extern "C" artdcevm::abi::arm64::android11::ReleaseObjectPointer BridgeDecode(
 artdcevm::abi::arm64::'''+('android14::DecodeGlobalJObject' if modern else 'android11::DecodeJObject')+''' fn,const void* self,jobject obj) {return fn(self,obj);}
extern "C" art::ObjPtr<art::mirror::Object> SourceNull() {return {};}
''')
    ir=unit.with_suffix('.ll')
    command=[args.arm64_cxx,'--target=aarch64-linux-android30','-std=c++17','-Wall','-Wextra','-Werror','-Wno-return-type-c-linkage','-O2','-S','-emit-llvm','-I',str(root/'artdcevm/src/main/cpp'),str(unit),'-o',str(ir)]
    result=subprocess.run(command,capture_output=True,text=True)
    unit.with_suffix('.log').write_text(result.stdout+result.stderr)
    result.check_returncode()
    text=ir.read_text()
    for wrapper in ('SourceDecode','BridgeDecode'):
        body=re.search(r'define[^\n]*@'+wrapper+r'\(.*?\n}',text,re.S).group()
        assert re.search(r'define[^\n]* i64 @'+wrapper,body)
        assert 'sret(' not in body and 'byval(' not in body
        assert re.search(r'call[^\n]*i64[^\n]*\(ptr[^,]*, ptr[^)]*\)',body)
    null=re.search(r'define[^\n]*@SourceNull\(.*?\n}',text,re.S).group()
    assert 'ret i64 0' in null
    report.append({'tag':tag,'named_decoder':name,'release_return':'trivial uintptr_t in x0; no sret/byval','null_return':0,'command':command})
(output/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
print('JNI object decoder literal source ABI: eight versions passed')
