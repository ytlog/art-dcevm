#!/usr/bin/env python3
"""Verify fixed ART AddGlobalRef ObjPtr parameter lowering for Android 11–17."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--arm64-cxx',required=True)
parser.add_argument('--source-dir',type=Path,default=Path('build/evidence/compatibility/20261008/jni-global-ref-source'))
parser.add_argument('--objptr-source-dir',type=Path,default=Path('build/evidence/compatibility/20261008/native-access-source'))
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
records=json.loads((args.source_dir/'manifest.json').read_text())
obj_records=[]
for name in ('manifest.json','broker-manifest.json','check-jni-manifest.json'):
    obj_records+=json.loads((args.objptr_source_dir/name).read_text())
output=root/'build/evidence/host/jni-global-ref-arm64';output.mkdir(parents=True,exist_ok=True)
def read(directory,records,tag,path):
    record=next(r for r in records if r['tag']==tag and r['path']==path)
    assert record['url']==f"https://android.googlesource.com/platform/art/+/{record['commit']}/{path}?format=TEXT"
    data=(directory/tag/path).read_bytes();assert hashlib.sha256(data).hexdigest()==record['sha256']
    return data.decode()
report=[]
for tag in sorted({r['tag'] for r in records}):
    header=read(args.source_dir,records,tag,'runtime/jni/java_vm_ext.h')
    assert 'jobject AddGlobalRef(Thread* self, ObjPtr<mirror::Object> obj)' in header
    implementation=read(args.source_dir,records,tag,'runtime/jni/java_vm_ext.cc')
    declaration=re.search(r'jobject JavaVMExt::AddGlobalRef\(Thread\* self, ObjPtr<mirror::Object> obj\)',implementation).group().replace('JavaVMExt::','')+';'
    obj=read(args.objptr_source_dir,obj_records,tag,'runtime/obj_ptr.h')
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
'''+re.sub(r'^#include.*$','',obj,flags=re.M)+'''
#include "platform/abi/arm64/android-common/jni_global_ref.h"
namespace art {class Thread;class JavaVMExt {public:
'''+declaration+'''
};}
static_assert(std::is_trivially_copyable_v<art::ObjPtr<art::mirror::Object>>);
static_assert(!art::kObjPtrPoisoning);
static_assert(!art::kObjPtrPoisoningValidateOnCopy);
extern "C" jobject SourceAdd(art::JavaVMExt* vm,art::Thread* self,art::ObjPtr<art::mirror::Object> object) {
 return vm->AddGlobalRef(self,object);
}
extern "C" jobject BridgeAdd(artdcevm::abi::arm64::android_common::AddGlobalRef fn,void* vm,void* self,
                            artdcevm::abi::arm64::android11::ReleaseObjectPointer object) {
 return fn(vm,self,object);
}
''')
    ir=unit.with_suffix('.ll')
    command=[args.arm64_cxx,'--target=aarch64-linux-android30','-std=c++17','-Wall','-Wextra','-Werror','-Wno-return-type-c-linkage','-O2','-S','-emit-llvm','-I',str(root/'artdcevm/src/main/cpp'),str(unit),'-o',str(ir)]
    result=subprocess.run(command,capture_output=True,text=True);unit.with_suffix('.log').write_text(result.stdout+result.stderr);result.check_returncode()
    text=ir.read_text()
    for wrapper in ('SourceAdd','BridgeAdd'):
        body=re.search(r'define[^\n]*@'+wrapper+r'\(.*?\n}',text,re.S).group()
        assert 'sret(' not in body and 'byval(' not in body
        assert re.search(r'call[^\n]*ptr[^\n]*\(ptr[^,]*, ptr[^,]*, i64[^)]*\)',body),body
    symbol='_ZN3art9JavaVMExt12AddGlobalRefEPNS_6ThreadENS_6ObjPtrINS_6mirror6ObjectEEE'
    assert symbol in text
    report.append({'tag':tag,'symbol':symbol,'parameter':'ObjPtr lowered to i64 in x2; this x0; Thread* x1; return jobject x0','command':command})
(output/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
print('JNI AddGlobalRef literal source parameter ABI: eight versions passed')
