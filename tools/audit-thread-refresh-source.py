#!/usr/bin/env python3
"""Compile ARM64 offsetof proofs from verified fixed AOSP field declarations.

Requires ignored source cache populated from official Gitiles. Never calibrates a
live Thread or emits a runtime permission token.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
source = root / 'build/evidence/compatibility/20261008/thread-refresh-source'
output = root / 'build/evidence/host/thread-refresh-source'
output.mkdir(parents=True, exist_ok=True)
manifest = json.loads((source / 'manifest.json').read_text())
cpp = root / 'artdcevm/src/main/cpp'
ndk = Path(os.environ.get('ANDROID_NDK_HOME', str(Path.home() / 'Library/Android/sdk/ndk/27.2.12479018')))
compiler = ndk / 'toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android33-clang++'

def verified(tag, path):
    record = next(r for r in manifest if r['tag'] == tag and r['path'] == path)
    p = source / tag / path
    if path == 'runtime/thread.h':
        p = root / 'build/evidence/compatibility/20261008/aosp' / tag / path
    data = p.read_bytes()
    assert hashlib.sha256(data).hexdigest() == record['sha256'], str(p)
    assert record['url'].startswith('https://android.googlesource.com/platform/art/+/')
    return data.decode()

def no_comments(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)

def block(text, start, end):
    return no_comments(text[text.index(start):text.index(end, text.index(start))])

records = []
for version in (16, 17):
    tag = f'android-{version}.0.0_r1'
    thread = verified(tag, 'runtime/thread.h')
    managed = verified(tag, 'runtime/managed_stack.h')
    managed = managed[:managed.index('ShadowFrame* top_shadow_frame_;')+len('ShadowFrame* top_shadow_frame_;')]
    stats = verified(tag, 'runtime/runtime_stats.h')
    atomic = verified(tag, 'libartbase/base/atomic.h')
    macros = verified(tag, 'libartbase/base/macros.h')
    assert '#define PACKED(x) __attribute__ ((__aligned__(x), __packed__))' in macros
    assert 'class PACKED(sizeof(T)) Atomic : public std::atomic<T>' in atomic
    assert 'using AtomicInteger = Atomic<int32_t>;' in atomic
    assert 'class EXPORT Thread {' in thread
    thread_body = thread[thread.index('class EXPORT Thread {'):thread.index('} tls32_;')]
    assert not re.search(r'^\s*virtual\b', thread_body, re.M)
    # The constructor is omitted; the entire non-static data declaration sequence
    # is retained verbatim, including atomics/enum storage and source alignment.
    tls32 = block(thread, 'Atomic<uint32_t> state_and_flags;', '} tls32_;')
    tls32 = re.sub(r'static_assert\(.*?\);', '', tls32, flags=re.S)
    tls32 = re.sub(r'GUARDED_BY\([^\n]*?\)', '', tls32)
    tls64 = block(thread, 'uint64_t trace_clock_base;', '} tls64_;')
    ptr = block(thread, 'uint8_t* card_table;', 'JNIEnvExt* jni_env;')
    weak = block(thread, 'enum class WeakRefAccessState : int32_t {', '// ART uses')
    # Only the seven uint64_t instance fields appear in RuntimeStats. Preserve
    # declaration order independently rather than copying the production mirror.
    stats_fields = '\n'.join(re.findall(r'^\s*uint64_t\s+\w+;', stats, re.M))
    assert len(re.findall(r'uint64_t', stats_fields)) == 7
    tagged = re.search(r'^\s*uintptr_t tagged_sp_;', managed, re.M).group(0)
    stack_fields = '\n'.join(re.findall(r'^\s*(?:TaggedTopQuickFrame|ManagedStack\*|ShadowFrame\*)\s+\w+;', managed, re.M))
    assert len(stack_fields.split(';')) == 4
    generated = '''#include <atomic>
#include <cstddef>
#include <cstdint>
#include "platform/common/thread_refresh.h"
namespace oracle {
struct ShadowFrame; namespace mirror { struct Throwable; }
template<class T> struct __attribute__((packed,aligned(sizeof(T)))) Atomic : std::atomic<T> {};
using AtomicInteger = Atomic<int32_t>;
'''+weak+'''
struct __attribute__((packed,aligned(4))) RuntimeStats { '''+stats_fields+''' };
struct __attribute__((packed,aligned(4))) ManagedStack {
struct TaggedTopQuickFrame { '''+tagged+''' }; '''+stack_fields+''' };
struct ThreadPrefix {
struct alignas(4) Tls32 { using bool32_t=uint32_t; '''+tls32+''' } tls32_;
struct alignas(8) Tls64 { '''+tls64+''' } tls64_;
struct alignas(sizeof(void*)) TlsPtr { '''+ptr+''' } tlsPtr_;
};
}
using Mirror = artdcevm::abi::arm64::android'''+str(version)+'''::ThreadTlsPrefix;
static_assert(sizeof(Mirror)==sizeof(oracle::ThreadPrefix));
static_assert(alignof(Mirror)==alignof(oracle::ThreadPrefix));
static_assert(sizeof(decltype(Mirror::tls32_))==sizeof(oracle::ThreadPrefix::Tls32));
static_assert(sizeof(decltype(Mirror::tls64_))==sizeof(oracle::ThreadPrefix::Tls64));
static_assert(sizeof(decltype(Mirror::tlsPtr_))==sizeof(oracle::ThreadPrefix::TlsPtr));
'''
    for group, declarations in [('tls32_', tls32), ('tls64_', tls64), ('tlsPtr_', ptr)]:
        for declaration in declarations.split(';'):
            if not declaration.strip():
                continue
            name = re.search(r'(\w+)\s*$', declaration).group(1)
            generated += f'static_assert(offsetof(Mirror,{group})+offsetof(decltype(Mirror::{group}),{name}) == offsetof(oracle::ThreadPrefix,{group})+offsetof(decltype(oracle::ThreadPrefix::{group}),{name}));\n'
    generated += '''extern "C" const size_t source_refresh_offsets[] = {
 offsetof(oracle::ThreadPrefix,tls32_)+offsetof(oracle::ThreadPrefix::Tls32,is_gc_marking),
 offsetof(oracle::ThreadPrefix,tlsPtr_)+offsetof(oracle::ThreadPrefix::TlsPtr,suspend_trigger),
 sizeof(oracle::ThreadPrefix)};
'''
    unit = output / f'android{version}-source-proof.cc'
    unit.write_text(generated)
    command = [str(compiler), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(cpp), '-c', str(unit), '-o', str(output / f'android{version}-source-proof.o')]
    result = subprocess.run(command, capture_output=True, text=True)
    (output / f'android{version}-source-proof.log').write_text(result.stdout+result.stderr)
    result.check_returncode()
    records.append({'tag':tag,'command':command,'source_field_offsets_compared':True,'runtime_calibrated':False,'proof_sha256':hashlib.sha256(generated.encode()).hexdigest(), 'prerequisites':['fixed libart identity/source family independently calibrated','ARM64 LP64 compiler atomics/packing checked','read-barrier build mode independently known','owned current-thread oracle must independently validate refresh fields; source mirror is not oracle','thread lifetime and valid mapped TLS prefix must be proven before any runtime read'], 'scope':'entire TLS32 and TLS64; pointer prefix through suspend_trigger only, not full Thread sizeof'})
(output / 'audit.json').write_text(json.dumps(records,indent=2)+'\n')
print('fixed16/17 source field offsetof vs production mirror: NDK ARM64 passed; no runtime calibration')
