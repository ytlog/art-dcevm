#!/usr/bin/env python3
"""Build the isolated AOSP-source engine; this does not establish device ABI compatibility."""
import hashlib
import json
import os
import shutil
import sys
from pathlib import Path
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/art13-source'
OUT.mkdir(parents=True, exist_ok=True)
sdk = os.environ.get('ANDROID_SDK_ROOT') or os.environ.get('ANDROID_HOME')
if not sdk:
    sdk = next(line.split('=', 1)[1] for line in (ROOT / 'local.properties').read_text().splitlines() if line.startswith('sdk.dir='))
host = {'darwin': 'darwin-x86_64', 'linux': 'linux-x86_64'}[sys.platform]
toolchain = Path(sdk) / 'ndk/27.2.12479018/toolchains/llvm/prebuilt' / host / 'bin'
compiler = toolchain / 'aarch64-linux-android33-clang++'

# Empty SONAME stubs provide DT_NEEDED entries at link time. All ART symbols
# remain undefined in the source engine and resolve from the device at load
# time. The loader checks required named symbols before loading, then runs the
# object-layout ABI probe before installing source hooks. No device library is a build input.
stub_dir = OUT / 'link-stubs'
stub_dir.mkdir(exist_ok=True)
required_libs = ('libopenjdkjvmti.so', 'libart.so', 'libartbase.so',
                 'libdexfile.so', 'libbase.so', 'libc++.so')
for name in required_libs:
    subprocess.run([str(compiler), '-shared', '-nostdlib', '-x', 'c++', '-',
                    '-Wl,-soname,' + name, '-o', str(stub_dir / name)],
                   input='extern "C" void artdcevm_link_stub() {}\n',
                   text=True, check=True, capture_output=True)
if '--prepare-link-stubs-only' in sys.argv:
    print(stub_dir)
    raise SystemExit(0)
CACHE = OUT / 'archives'
CACHE.mkdir(exist_ok=True)
lock = json.loads((Path(__file__).parent / 'sources.lock.json').read_text())
for item in lock['archives']:
    archive = CACHE / (item['destination'].replace('/', '_') + '.tar.gz')
    if not archive.exists():
        subprocess.run(['curl', '-fL', '--retry', '2', '--max-time', '120', item['url'], '-o', str(archive)], check=True)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != item['sha256']:
        raise SystemExit(f'Source archive hash mismatch: {archive}')
    dest = OUT / item['destination']
    stamp = dest / '.artdcevm-source-sha256'
    if not stamp.exists() or stamp.read_text() != item['sha256']:
        dest.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as files:
            members = [entry for entry in files.getmembers() if entry.isfile() or entry.isdir()]
            for entry in members:
                if entry.name.startswith('/') or '..' in Path(entry.name).parts:
                    raise SystemExit(f'Unsafe archive path: {entry.name}')
            # AOSP test-only symlinks point outside the archive. The compiler inputs
            # are regular files; do not materialize those unrelated links.
            files.extractall(dest, members=members)
        stamp.write_text(item['sha256'])

# Headers include neighbours with quotes, so an include-path-only overlay would
# still pick some original inline definitions. Copy the runtime headers' directory
# topology, then patch the isolated copy; keep the pinned archive untouched.
runtime_overlay = OUT / 'modified/runtime'
shutil.copytree(OUT / 'art/runtime', runtime_overlay, dirs_exist_ok=True)
for header_patch in ['read_barrier-inl.patch', 'reflective_value_visitor.patch',
                     'jit_code_cache.patch']:
    subprocess.run(['patch', '--batch', '--fuzz=0', '-p0', '-i',
                    str(ROOT / 'artdcevm/src/main/cpp/platform/android13/source_engine' / header_patch)],
                   cwd=runtime_overlay, check=True, capture_output=True, text=True)
includes = ['modified/runtime', 'art/compiler', 'art/libartbase', 'art/libdexfile', 'art/openjdkjvmti',
            'art/openjdkjvmti/include', 'art/libartpalette/include', 'art/cmdline',
            'libbase/include', 'libnativehelper/include', 'libnativehelper/header_only_include',
            'bionic/platform']
# AOSP art/build/{Android.bp,art.go}; this is one build profile, not an API-wide ABI promise.
defines = ['NDEBUG', 'ART_TARGET', 'ART_TARGET_ANDROID', 'ART_DEFAULT_GC_TYPE_IS_CMS',
           'ART_USE_READ_BARRIER=1', 'ART_READ_BARRIER_TYPE_IS_BAKER=1',
           'ART_USE_GENERATIONAL_CC=1', 'ART_DEFAULT_COMPACT_DEX_LEVEL=fast',
           'ART_BASE_ADDRESS=0x70000000', 'ART_BASE_ADDRESS_MIN_DELTA=-0x1000000',
           'ART_BASE_ADDRESS_MAX_DELTA=0x1000000', 'Redefiner=SourceRedefiner']
defines += [f'ART_STACK_OVERFLOW_GAP_{arch}=8192' for arch in ('arm', 'arm64', 'x86', 'x86_64')]
flags = ['-std=c++17', '-fPIC', '-O2', '-fno-rtti', '-fno-exceptions',
         '-Wno-invalid-offsetof', '-Wno-deprecated-builtins', '-nostdinc++',
         '-isystem', str(OUT / 'libcxx/include'), '-isystem', str(OUT / 'bionic/include')]
flags += ['-D' + define for define in defines] + ['-I' + str(OUT / inc) for inc in includes]
engine = ROOT / 'artdcevm/src/main/cpp/platform/android13/source_engine'
flags += ['-I' + str(engine)]
flags += ['-I' + str(ROOT / 'artdcevm/src/main/cpp')]
generated = OUT / 'modified'
generated.mkdir(exist_ok=True)
shutil.copyfile(OUT / 'art/openjdkjvmti/ti_redefine.cc', generated / 'ti_redefine.cc')
subprocess.run(['patch', '--batch', '--fuzz=0', '-p0', '-i', str(engine / 'ti_redefine.patch')],
               cwd=generated, check=True, capture_output=True, text=True)
subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(engine / 'selective_deopt.patch')],
               cwd=generated, check=True, capture_output=True, text=True)
sources = [generated / 'ti_redefine.cc', engine / 'entry.cc', engine / 'retired_methods.cc',
           engine / 'retired_fields.cc', engine / 'runtime_services.cc', engine / 'hierarchy.cc']
commands = []
objects = []
for source in sources:
    obj = OUT / (source.stem + '.o')
    command = [str(compiler), *flags, '-c', str(source), '-o', str(obj)]
    commands.append(command)
    result = subprocess.run(command, capture_output=True, text=True)
    (OUT / (source.stem + '.log')).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    objects.append(str(obj))

exports = OUT / 'source-engine.map'
exports.write_text('{ global: ArtDcevmSource*; ArtDcevmGuard*; local: *; };\n')
library = OUT / 'libartdcevm_source13.so'
command = [str(compiler), '-shared', '-nostdlib++', '-Wl,--no-as-needed', '-Wl,-Bsymbolic',
           '-Wl,--version-script=' + str(exports), '-Wl,-soname,' + library.name,
           *objects, '-L' + str(stub_dir), '-lopenjdkjvmti', '-lart',
           '-lartbase', '-ldexfile', '-lbase', '-lc++', '-llog', '-o', str(library)]
commands.append(command)
result = subprocess.run(command, capture_output=True, text=True)
(OUT / 'link.log').write_text(result.stdout + result.stderr)
(OUT / 'commands.json').write_text(json.dumps(commands, indent=2))
if result.returncode:
    raise SystemExit(result.stdout + result.stderr)
report = {'sourceTag': lock['tag'], 'compileAndLinkPassed': True,
          'deviceAbiVerified': False, 'fullDcevmComplete': False,
          'librarySha256': hashlib.sha256(library.read_bytes()).hexdigest(),
          'note': 'Source-compiled structural commit and member ABI probe; original ART owns allocation and transaction setup.'}
(OUT / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
print(library)
