#!/usr/bin/env python3
"""Compile eight literal ART Context declarations and verify shared ARM64 virtual prefix."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--arm64-cxx',required=True)
p.add_argument('--source-dir',type=Path,default=Path('build/evidence/compatibility/20261008/owned-context-prefix-source'))
a=p.parse_args();root=Path(__file__).resolve().parents[1]
records=json.loads((a.source_dir/'manifest.json').read_text())
out=root/'build/evidence/host/owned-context-prefix-arm64';out.mkdir(parents=True,exist_ok=True)
def read(tag,path):
 r=next(r for r in records if r['tag']==tag and r['path']==path)
 assert r['url']==f"https://android.googlesource.com/platform/art/+/{r['commit']}/{path}?format=TEXT"
 b=(a.source_dir/tag/path).read_bytes();assert hashlib.sha256(b).hexdigest()==r['sha256'];return b.decode()
def clean(s):return re.sub(r'^#include.*$','',s,flags=re.M)
methods=[('Reset','',2),('SetSP',', uintptr_t value',4),('SetPC',', uintptr_t value',5),('SetArg0',', uintptr_t value',6),('IsAccessibleGPR',', uint32_t reg',7),('GetGPRAddress',', uint32_t reg',8),('GetGPR',', uint32_t reg',9)]
report=[]
for tag in sorted({r['tag'] for r in records}):
 context=read(tag,'runtime/arch/context.h');arm=read(tag,'runtime/arch/arm64/context_arm64.h');cc=read(tag,'runtime/arch/arm64/context_arm64.cc');regs=read(tag,'runtime/arch/arm64/registers_arm64.h')
 assert 'static constexpr size_t kPC = kNumberOfXRegisters;' in arm
 assert all(s in cc for s in ('gprs_[SP] = &sp_;','gprs_[kPC] = &pc_;','gprs_[X0] = &arg0_;','arg0_ = 0;'))
 unit=out/(tag+'.cc')
 code='''#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <iosfwd>
#define HIDDEN
#define EXPORT
#define NO_RETURN __attribute__((noreturn))
#define ATTRIBUTE_UNUSED __attribute__((unused))
namespace art {
enum class InstructionSet {kX86,kX86_64,kArm64};
constexpr size_t GetInstructionSetPointerSize(InstructionSet){return 8;}
}
'''+clean(context)+clean(regs)+'''
#include "platform/abi/arm64/common/context.h"
static_assert(art::arm64::SP==31);
static_assert(art::arm64::kNumberOfXRegisters==33);
'''
 for name,params,slot in methods:
  result='bool' if name=='IsAccessibleGPR' else 'uintptr_t*' if name=='GetGPRAddress' else 'uintptr_t' if name=='GetGPR' else 'void'
  arg='value' if 'value' in params else 'reg' if 'reg' in params else ''
  for prefix,typ in [('Source','art::Context'),('Bridge','artdcevm::abi::arm64::android_common::ContextPrefix')]:
   code+=f'extern "C" {result} {prefix}{name}({typ}* self{params}) {{return self->{name}({arg});}}\n'
 for prefix,typ in [('Source','art::Context'),('Bridge','artdcevm::abi::arm64::android_common::ContextPrefix')]:code+=f'extern "C" void {prefix}Destroy({typ}* self) {{delete self;}}\n'
 code+='extern "C" art::Context* SourceCreate(){return art::Context::Create();}\n'
 unit.write_text(code);ir=unit.with_suffix('.ll')
 cmd=[a.arm64_cxx,'--target=aarch64-linux-android30','-std=c++17','-Wall','-Wextra','-Werror','-O2','-S','-emit-llvm','-I',str(root/'artdcevm/src/main/cpp'),str(unit),'-o',str(ir)]
 r=subprocess.run(cmd,capture_output=True,text=True);unit.with_suffix('.log').write_text(r.stdout+r.stderr);r.check_returncode();text=ir.read_text()
 for name,params,slot in methods+[('Destroy','',1)]:
  for prefix in ('Source','Bridge'):
   body=re.search(r'define[^\n]*@'+prefix+name+r'\(.*?\n}',text,re.S).group()
   assert re.search(r'getelementptr[^\n]*ptr[^\n]*i64 '+str(slot)+r'\b',body),body
   assert 'sret(' not in body and 'byval(' not in body
 assert '_ZN3art7Context6CreateEv' in text
 report.append({'tag':tag,'shared_virtual_slots':{n:s for n,_,s in methods+[('Destroy','',1)]},'native_registers':{'sp':31,'pc':33,'arg0':0},'tail':'not invoked','command':cmd})
(out/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
print('Owned Context literal shared virtual prefix: eight Android versions passed ARM64 compiler proof')
