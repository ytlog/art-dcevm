#!/usr/bin/env python3
"""Compare modern bridge ABI to literal fixed ART source declarations on ARM64.

Requires the previously hash-verified fixed-source cache. Compilation strips
platform includes and annotation macros; class/union/enum declarations come
from the source files, not the bridge header or host mock.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--arm64-cxx', required=True)
parser.add_argument('--source-dir', type=Path, default=Path('build/evidence/compatibility/20261008/deoptimize-if-needed'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
base = root / 'build/evidence/compatibility/20261008/aosp'
manifest = json.loads((args.source_dir / 'additional-sources.json').read_text())
manifest += json.loads((base / 'source-manifest.json').read_text())
out = root / 'build/evidence/host/modern-thread-deopt/arm64-source-abi'
out.mkdir(parents=True, exist_ok=True)
def read(tag, path):
    entries = [e for e in manifest if e['tag'] == tag and e['path'] == path and e.get('ok', True)]
    assert entries
    file = args.source_dir / tag / path
    if not file.exists(): file = base / tag / path
    data = file.read_bytes()
    assert hashlib.sha256(data).hexdigest() == entries[-1]['sha256']
    return data.decode()
def strip_includes(source):
    return re.sub(r'^#include.*$', '', source, flags=re.M)
report=[]
for tag in ('android-16.0.0_r1','android-17.0.0_r1'):
    directory = out / tag
    directory.mkdir(exist_ok=True)
    thread = read(tag, 'runtime/thread.h')
    method_type = re.search(r'enum class DeoptimizationMethodType\s*\{.*?\};', thread, re.S).group()
    kind = re.search(r'enum class DeoptimizationKind\s*\{.*?\};', read(tag,'runtime/deoptimization_kind.h'), re.S).group()
    push = re.search(r'void PushDeoptimizationContext\(.*?;', thread, re.S).group()
    deopt = re.search(r'std::unique_ptr<Context> Deoptimize\(.*?;', thread, re.S).group()
    prelude = '''#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <type_traits>
#define HIDDEN
#define REQUIRES_SHARED(...)
#define REQUIRES(...)
#define ALWAYS_INLINE inline
#define NO_THREAD_SAFETY_ANALYSIS
#define EXPORT
#define PACKED(x) __attribute__((packed,aligned(x)))
#define DCHECK(x) ((void)0)
namespace art {
constexpr bool kIsDebugBuild=false;
constexpr size_t kHeapReferenceSize=4,kBitsPerByte=8,kObjectAlignmentShift=3;
namespace mirror {class Object;class Throwable;}
// Unused static Context register-address helper dependencies, not call ABI.
enum class InstructionSet {kX86,kX86_64,kArm64};
constexpr size_t GetInstructionSetPointerSize(InstructionSet) {return 8;}
}
'''
    literal = prelude + strip_includes(read(tag,'runtime/obj_ptr.h'))
    literal += strip_includes(read(tag,'runtime/jvalue.h'))
    literal += strip_includes(read(tag,'runtime/arch/context.h'))
    literal += '\nnamespace art {\n' + method_type + '\n' + kind + '\nclass Thread {public:\n' + push + '\n' + deopt + '\n};\n}\n'
    (directory / 'literal-native-source.h').write_text(literal)
    ir = directory / 'proof.ll'
    subprocess.run([args.arm64_cxx,'--target=aarch64-linux-android30','-std=c++17',
                    '-Wall','-Wextra','-Werror','-O2','-S','-emit-llvm',
                    '-I',str(root / 'artdcevm/src/main/cpp'),'-I',str(directory),
                    str(root / 'tests/host/modern_thread_deopt_arm64_abi.cc'),'-o',str(ir)],check=True)
    text = ir.read_text()
    def body(name):
        return re.search(r'define[^\n]*@'+name+r'\(.*?\n\}',text,re.S).group()
    source_push,bridge_push=body('SourcePush'),body('BridgePush')
    for function in (source_push,bridge_push):
        assert re.search(r'call void .*?\(ptr .*?, ptr .*?, i1 .*?, i64 0, i1 .*?false, i32 ',function)
    for name in ('SourceFullDeopt','BridgeFullDeopt'):
        function=body(name)
        assert 'sret(' in function and re.search(r'i32 .*?8, i1 .*?false, i1 .*?false',function)
    report.append({'tag':tag,'releaseObjPtr':'source nullptr constructor0; trivially copyable uintptr word i64 argument',
                   'PushContext':'thread ptr / const JValue ref / is_ref i1 / null ObjPtr i64 0 / from_code false / MethodType i32',
                   'Deoptimize':'nontrivial unique_ptr sret / FullFrame i32 8 / single_frame false / skip_exit false'})
(out / 'result.json').write_text(json.dumps(report,indent=2)+'\n')
print('Literal fixed-source ARM64 ABI: PushContext and nontrivial full Thread deopt passed for 16/17')
