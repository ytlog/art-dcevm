#!/usr/bin/env python3
"""Compare the JIT ABI prefix against pinned AOSP declarations with an Android compiler.

The manifest supplies local paths and SHA-256 values for the eight fixed-source
files recorded by the source audit. Raw sources and generated output stay local.
This checks the source ABI, not arbitrary vendor/APEX runtime compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from evidence_paths import new_evidence_directory
ROOT = Path(__file__).resolve().parents[1]
PINS = {
    'jit_code_cache16.cc': 'c900f29a2c35a4914cfb4541196b91d99ca63594b813d59f3cf1caf65fe32c2a',
    'jit_memory_region16.h': 'ed6dac422a7ea3564a0d9a5889ce505a4d01aaf7773d2e4520c823ba0da47bae',
    'mem_map16.h': 'ac1e3dc8ff5f6ab8efeaba4a5cbdaf9d858b7dcc7d5697b23250475075b68ea1',
    'safe_map16.h': '4d7a17199fa8a31ab6e82e248fa5c21a61877b79d8f56f2d4c4ef3c12ced0d95',
    'array_ref16.h': '85514afad62004cc816d59325bcdd2bd12f42408333078b193be92829c8e8125',
    'atomic16.h': '86d918e07788a0b5b720789280f640feb5a8e0dfd9058b09d08ebd970b3da19a',
    'jit_code_cache.h': '6e9e79917eb4961da50c5052a42cad2b6a3138682205ebefb7d367bfa9d900cf',
    'mutex.h': '4ab54798454ff436638b5441daffe2d11fcf74d6732804379fc9909bf9afedfd',
}
def block(text, first, last):
    text = text[text.index(first):]
    return text[:text.index(last) + len(last)]
def clean(text):
    text = re.sub(r'//[^\n]*', '', text)
    return re.sub(r'\s+GUARDED_BY\([^)]*\)', '', text)
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-manifest', type=Path, required=True)
    parser.add_argument('--cxx', type=Path, required=True,
                        help='NDK aarch64-linux-android30-clang++ executable')
    args = parser.parse_args()
    manifest = json.loads(args.source_manifest.read_text())
    assert manifest['tag'] == 'android-16.0.0_r1'
    sources = {}
    for name, expected in PINS.items():
        paths = [ROOT / p for p in manifest['files'] if Path(p).name == name]
        assert len(paths) == 1, name
        data = paths[0].read_bytes()
        assert hashlib.sha256(data).hexdigest() == expected, name
        sources[name] = data.decode()
    evidence = new_evidence_directory(ROOT / 'build/evidence/host/jit-layout')
    parts = ['#include "platform/abi/arm64/android16/jit_code_cache.h"',
             '#if !defined(__ANDROID__) || !defined(__aarch64__)\n#error Android arm64 required\n#endif',
             'namespace reference {',
             'struct Mutex; struct ArtMethod; struct ProfilingInfo; struct CodeCacheBitmap;',
             'enum class ZygoteCompilationState : uint8_t;',
             'template<class T> struct __attribute__((packed, aligned(sizeof(T)))) Atomic : std::atomic<T> {};',
             'using AtomicInteger = Atomic<int32_t>;']
    # Keep composition/reference/unique_ptr types distinct from the ABI proxies.
    assert 'Atomic : public std::atomic<T>' in sources['atomic16.h']
    safe = sources['safe_map16.h']
    parts += [block(safe, 'template <typename K, typename V, typename Comparator', 'class SafeMap {') +
              '\npublic:\n' + block(safe, 'using Impl =', ';') + '\n' + block(safe, 'Impl map_;', 'Impl map_;') + '\n};']
    parts += ['template<class T> struct ArrayRef {' + block(sources['array_ref16.h'], 'T* array_;', 'size_t size_;') + '};']
    condition = block(sources['mutex.h'][sources['mutex.h'].index('class EXPORT ConditionVariable {'):],
                      'const char* const name_;', 'int32_t num_waiters_;').replace('#if ART_USE_FUTEXES', '')
    declarations = {
        'ConditionVariable': condition,
        'MemMap': block(sources['mem_map16.h'], 'std::string name_;', 'size_t redzone_size_ = 0u;'),
        'JitMemoryRegion': block(sources['jit_memory_region16.h'], 'size_t initial_capacity_', 'void* exec_mspace_ GUARDED_BY(Locks::jit_lock_);'),
        'JniStubKey': block(sources['jit_code_cache16.cc'], 'mutable const char* shorty_;', 'const bool is_synchronized_;'),
        'JniStubData': block(sources['jit_code_cache16.cc'], 'const void* code_;', 'std::vector<ArtMethod*> methods_;'),
        'ZygoteMap': 'struct Entry { ArtMethod* method; const void* code; };\n' + block(sources['jit_code_cache.h'], 'ArrayRef<const Entry> map_;', 'const ZygoteCompilationState* compilation_state_;'),
        'JitCodeCache': block(sources['jit_code_cache.h'], 'Atomic<bool> is_weak_access_enabled_;', 'std::set<const void*> processed_zombie_code_ GUARDED_BY(Locks::jit_lock_);'),
    }
    for name, fields in declarations.items():
        parts.append('struct ' + name + ' {\n' + clean(fields) + '\n};')
    parts += ['}', 'namespace local = artdcevm::abi::arm64::android16;',
              '#define FIELD(R,L,A,B) static_assert(__builtin_offsetof(reference::R,A)==__builtin_offsetof(local::L,B))',
              '#define SIZE(R,L) static_assert(sizeof(reference::R)==sizeof(local::L)); static_assert(alignof(reference::R)==alignof(local::L))']
    for r,l in [('ConditionVariable','JitConditionVariable'),('MemMap','JitMemMap'),('JitMemoryRegion','JitMemoryRegion'),('JniStubKey','JitStubKey'),('JniStubData','JitStubData'),('ZygoteMap','JitZygoteMap'),('JitCodeCache','JitCodeCachePrefix')]:
        parts.append(f'SIZE({r},{l});')
    for ref, proxy in [('is_weak_access_enabled_','weak_access_enabled'),('inline_cache_cond_','inline_cache_condition'),('reserved_capacity_','reserved_capacity'),('shared_region_','shared_region'),('private_region_','private_region'),('jni_stubs_map_','jni_stubs'),('method_code_map_','method_code'),('method_code_map_reversed_','reversed_method_code'),('saved_compiled_methods_map_','saved_compiled_methods'),('osr_code_map_','osr_code'),('zombie_code_','zombie_code'),('zombie_jni_code_','zombie_jni_code'),('profiling_infos_','profiling_infos'),('zygote_map_','zygote'),('lock_cond_','collection_condition'),('collection_in_progress_','collection_in_progress'),('gc_task_scheduled_','gc_task_scheduled'),('live_bitmap_','live_bitmap'),('garbage_collect_code_','garbage_collect_code'),('processed_zombie_code_','processed_zombie_code')]:
        parts.append(f'FIELD(JitCodeCache,JitCodeCachePrefix,{ref},{proxy});')
    for r,l,a,b in [('JitMemoryRegion','JitMemoryRegion','exec_pages_','exec_pages'),('MemMap','JitMemMap','begin_','begin'),('MemMap','JitMemMap','size_','size')]:
        parts.append(f'FIELD({r},{l},{a},{b});')
    source = evidence / 'layout.cc'; source.write_text('\n'.join(parts) + '\n')
    result = subprocess.run([str(args.cxx), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                             '-Wno-invalid-offsetof',
                             '-I' + str(ROOT / 'artdcevm/src/main/cpp'), '-c', str(source),
                             '-o', str(evidence / 'layout.o')], capture_output=True, text=True)
    (evidence / 'compile.log').write_text(result.stdout + result.stderr)
    if result.returncode: raise RuntimeError('Layout comparison failed; inspect ignored evidence')
    (evidence / 'result.json').write_text(json.dumps({'result':'PASS','tag':manifest['tag'],'sourceHashes':PINS,'target':'Android arm64','fields':23}, indent=2)+'\n')
    print('PASS: pinned AOSP declaration sizes, alignment and 23 field positions match Android arm64 ABI proxies')
    print('Evidence: ' + str(evidence.relative_to(ROOT)))
if __name__ == '__main__': main()
