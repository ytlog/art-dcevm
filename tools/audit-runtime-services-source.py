#!/usr/bin/env python3
"""Check fixed Runtime prefixes against native ARM64 libc++ layout.

Reads the source cache produced by audit-art-source.py. Only already verified
fixed source bytes are used; this does not calibrate a loaded vendor Runtime.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-dir', type=Path, required=True)
parser.add_argument('--evidence-dir', type=Path, required=True)
parser.add_argument('--arm64-cxx', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
args.evidence_dir.mkdir(parents=True, exist_ok=True)
entries = json.loads((args.source_dir / 'source-manifest.json').read_text())
tags = ['android-' + version + '_r1' for version in
        ['11.0.0', '12.0.0', '12.1.0', '13.0.0', '14.0.0', '15.0.0', '16.0.0', '17.0.0']]
fields = ['class_linker', 'jni_id_manager', 'jit', 'jit_code_cache',
          'thread_list', 'heap', 'java_vm']
cpp = ['#include <string>', '#include <vector>', '#include <list>',
       '#include <memory>', '#include <atomic>', '#include <cstddef>',
       '#include "platform/common/runtime_services.h"', 'struct Opaque;',
       'static_assert(sizeof(std::string)==24 && sizeof(std::vector<int>)==24 && sizeof(std::list<int>)==24);',
       'static_assert(sizeof(std::unique_ptr<Opaque>)==8 && sizeof(std::atomic<void*>)==8);',
       '#pragma clang diagnostic push',
       '#pragma clang diagnostic ignored "-W' + 'invalid' + '-offsetof"']
records = []
for tag in tags:
    matches = [entry for entry in entries if entry.get('tag') == tag and
               entry.get('path') == 'runtime/runtime.h' and entry.get('ok')]
    if len(matches) != 1:
        raise ValueError('Expected one verified Runtime source: ' + tag)
    entry = matches[0]
    blob = (args.source_dir / tag / 'runtime/runtime.h').read_bytes()
    if hashlib.sha256(blob).hexdigest() != entry['sha256']:
        raise ValueError('Runtime source hash mismatch: ' + tag)
    text = blob.decode()
    start = text.index('  uint64_t callee_save_methods_')
    end = text.index('  std::unique_ptr<jit::JitOptions> jit_options_', start)
    declaration = re.sub(r'/\*.*?\*/', '', text[start:end], flags=re.S)
    declaration = '\n'.join(line.split('//')[0].strip()
                            for line in declaration.splitlines() if line.split('//')[0].strip())
    # Keep native libc++ headers and primitive field order from the actual source.
    # Opaque pointee types affect neither these pointer slots nor container headers.
    native = re.sub(r'GcRoot<[^>]+>', 'uint32_t', declaration)
    native = native.replace('kCalleeSaveSize', '6').replace('InstructionSet', 'uint32_t')
    if 'kCalleeSaveSize = 6u;' not in text:
        raise ValueError('Unexpected callee-save count: ' + tag)
    native = re.sub(r'std::unique_ptr<[^>]+>', 'std::unique_ptr<Opaque>', native)
    native = re.sub(r'std::atomic<[^>]+>', 'std::atomic<void*>', native)
    for original in ['std::vector<File>', 'std::vector<Plugin>']:
        native = native.replace(original, 'std::vector<Opaque>')
    native = native.replace('std::list<ti::AgentSpec>', 'std::list<Opaque>')
    native = re.sub(r'\b(ArtMethod|CompilerCallbacks|ThreadList|InternTable|ClassLinker|'
                    r'SignalCatcher|MonitorList|MonitorPool|SmallIrtAllocator)\*', 'void*', native)
    native = native.replace('gc::Heap*', 'void*').replace('jni::SmallLrtAllocator*', 'void*')
    version = tag.split('-')[1]
    major = int(version.split('.')[0])
    family = min(major, 15)
    name = 'Source' + version.replace('.', '_')
    cpp += ['struct ' + name + ' {', native, '};']
    for field in fields:
        cpp.append(f'static_assert(offsetof({name},{field}_)=='
                   f'offsetof(artdcevm::abi::arm64::android{family}::RuntimePrefix,{field}));')
    cpp.append(f'static_assert(sizeof({name})=='
               f'sizeof(artdcevm::abi::arm64::android{family}::RuntimePrefix));')
    records.append({**entry, 'declarationFamily': family,
                    'borrowedServiceFields': [field + '_' for field in fields],
                    'nativeDeclarations': declaration})
cpp.append('#pragma clang diagnostic pop')
proof = args.evidence_dir / 'native-prefix-proof.cc'
proof.write_text('\n'.join(cpp) + '\n')
(args.evidence_dir / 'source-audit.json').write_text(json.dumps(records, indent=2) + '\n')
command = [args.arm64_cxx, '--target=aarch64-linux-android30', '-std=c++17',
           '-Wall', '-Wextra', '-Werror', '-I', str(root / 'artdcevm/src/main/cpp'),
           '-c', str(proof), '-o', str(args.evidence_dir / 'native-prefix-proof.o')]
result = subprocess.run(command, capture_output=True, text=True)
(args.evidence_dir / 'native-prefix-proof.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print('Eight fixed Runtime sources: SHA-256, native ARM64 libc++ prefix sizes and service offsets passed')
