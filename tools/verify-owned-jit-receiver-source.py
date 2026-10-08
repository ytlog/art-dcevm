#!/usr/bin/env python3
"""Compile fixed ART JIT/GC declarations with the Android ARM64 NDK."""
import argparse,base64,hashlib,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--arm64-cxx',required=True)
p.add_argument('--source-dir',type=Path,default=Path('build/evidence/compatibility/20261008/owned-jit-source'))
a=p.parse_args();root=Path(__file__).resolve().parents[1]
commit='88baac34da072b41917b4adc1987c55546266b33'
paths=('runtime/jit/jit.h','runtime/jit/jit.cc','runtime/jit/jit_code_cache.h','runtime/gc/gc_cause.h','runtime/gc/heap.h','runtime/runtime.cc','openjdkjvmti/ti_heap.cc','runtime/compilation_kind.h')
a.source_dir.mkdir(parents=True,exist_ok=True)
manifest=[];sources={}
for path in paths:
 file=a.source_dir/path;url=f'https://android.googlesource.com/platform/art/+/{commit}/{path}?format=TEXT'
 if not file.exists():
  file.parent.mkdir(parents=True,exist_ok=True)
  response=subprocess.run(['curl','-fLSs','--max-time','45',url],capture_output=True,check=True)
  file.write_bytes(base64.b64decode(response.stdout))
 data=file.read_bytes();sources[path]=data.decode()
 manifest.append({'commit':commit,'path':path,'url':url,'sha256':hashlib.sha256(data).hexdigest()})
(a.source_dir/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
scope=re.search(r'class ScopedJitSuspend \{.*?\n};',sources['runtime/jit/jit.h'],re.S).group()
assert re.findall(r'\bbool\s+(\w+)\s*;',scope)==['was_on_']
cause=re.search(r'enum GcCause \{.*?\n};',sources['runtime/gc/gc_cause.h'],re.S).group()
assert 'bool CompileMethod(ArtMethod* method, Thread* self, CompilationKind compilation_kind, bool prejit)' in sources['runtime/jit/jit.h']
assert re.findall(r'k\w+',re.search(r'enum class CompilationKind \{.*?\n};',sources['runtime/compilation_kind.h'],re.S).group())==['kOsr','kBaseline','kOptimized']
assert 'void SweepRootTables(IsMarkedVisitor* visitor)' in sources['runtime/jit/jit_code_cache.h']
assert 'void CollectGarbage(bool clear_soft_references, GcCause cause = kGcCauseExplicit)' in sources['runtime/gc/heap.h']
assert 'GetJit()->GetCodeCache()->SweepRootTables(visitor);' in sources['runtime/runtime.cc']
assert 'art::Runtime::Current()->GetHeap()->CollectGarbage(/* clear_soft_references= */ false);' in sources['openjdkjvmti/ti_heap.cc']
assert 'jit->Stop();' in sources['runtime/jit/jit.cc'] and 'WaitForCompilationToFinish(self);' in sources['runtime/jit/jit.cc']
ctor=re.search(r'ScopedJitSuspend::ScopedJitSuspend\(\) \{.*?\n}',sources['runtime/jit/jit.cc'],re.S).group()
dtor=re.search(r'ScopedJitSuspend::~ScopedJitSuspend\(\) \{.*?\n}',sources['runtime/jit/jit.cc'],re.S).group()
out=root/'build/evidence/host/owned-jit-receiver-source';out.mkdir(parents=True,exist_ok=True)
unit=out/'source.cc';ir=out/'source.ll'
unit.write_text('''#include <cstddef>
#include "platform/android13/owned_jit_receiver_probe.h"
#define DCHECK(x) ((void)0)
namespace art {class Thread;class ArtMethod;class IsMarkedVisitor;enum class CompilationKind:int{kOsr,kBaseline,kOptimized};
namespace jit {
class Jit {public: void* GetThreadPool();void Stop();void Start();void WaitForCompilationToFinish(Thread*);bool CompileMethod(ArtMethod*,Thread*,CompilationKind,bool);};
class JitCodeCache {public: void SweepRootTables(IsMarkedVisitor*);};
'''+scope+'''
}
class Runtime {public: static Runtime* Current();jit::Jit* GetJit();};
namespace jit {
'''+ctor+'\n'+dtor+'''
void Jit::WaitForCompilationToFinish(Thread*) {}
void Jit::Stop() {}
void Jit::Start() {}
bool Jit::CompileMethod(ArtMethod*,Thread*,CompilationKind,bool) {return true;}
void JitCodeCache::SweepRootTables(IsMarkedVisitor*) {}
}
namespace gc {
'''+cause+'''
class Heap {public: void CollectGarbage(bool,GcCause);};
void Heap::CollectGarbage(bool,GcCause) {}
}
}
static_assert(sizeof(art::jit::ScopedJitSuspend)==1 && alignof(art::jit::ScopedJitSuspend)==1);
static_assert(sizeof(art::gc::GcCause)==4);
static_assert(sizeof(artdcevm::android13::owned_jit_probe_abi::SuspendJit)==sizeof(art::jit::ScopedJitSuspend));
''')
command=[a.arm64_cxx,'--target=aarch64-linux-android30','-std=c++17','-O1','-Wall','-Wextra','-Werror','-S','-emit-llvm','-I',str(root/'artdcevm/src/main/cpp'),str(unit),'-o',str(ir)]
result=subprocess.run(command,capture_output=True,text=True);(out/'compile.log').write_text(result.stdout+result.stderr);result.check_returncode()
text=ir.read_text();header=(root/'artdcevm/src/main/cpp/platform/android13/owned_jit_receiver_probe.h').read_text()
symbols=re.findall(r'inline constexpr char k\w+\[\]="([^"]+)";',header)
symbols.append("_ZN3art3jit3Jit13CompileMethodEPNS_9ArtMethodEPNS_6ThreadENS_15CompilationKindEb")
assert len(symbols)==5
for symbol in symbols:assert '@'+symbol+'(' in text,symbol
assert re.search(r'define.*@_ZN3art2gc4Heap14CollectGarbageEbNS0_7GcCauseE\(ptr[^,]*, i1[^,]*, i32',text)
assert 'sret(' not in text and 'byval(' not in text
(out/'audit.json').write_text(json.dumps({'source':manifest,'command':command,'symbols':symbols,'suspendJitBytes':1,'gcCauseBytes':4,'fixedSourceBodiesCompiled':True,'scope':'declarations and call ABI; not live receiver proof'},indent=2)+'\n')
print('Owned JIT receiver fixed source: ARM64 declarations, five named symbols, bool carrier and GC enum passed')
