#!/usr/bin/env python3
"""Verify fixed AOSP ArtMethod identity field order and ARM64 candidate declarations."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-dir', type=Path, default=root / 'build/evidence/compatibility/20261008/aosp')
parser.add_argument('--ndk', type=Path, default=Path.home() / 'Library/Android/sdk/ndk/27.2.12479018')
args = parser.parse_args()
out = root / 'build/evidence/host/method-identity-source'
out.mkdir(parents=True, exist_ok=True)
records = json.loads((args.source_dir / 'source-manifest.json').read_text())
selected = [record for record in records if record['path'] == 'runtime/art_method.h']
assert len(selected) == 8, len(selected)
report = []
for record in selected:
    source = (args.source_dir / record['tag'] / record['path']).read_bytes()
    assert hashlib.sha256(source).hexdigest() == record['sha256'], record
    assert re.fullmatch('[0-9a-f]{40}', record['commit'])
    text = source.decode()
    start = text.index('GcRoot<mirror::Class> declaring_class_;')
    end = text.index('} ptr_sized_fields_;', start) + len('} ptr_sized_fields_;')
    fields = re.sub(r'/\*.*?\*/|//[^\n]*', '', text[start:end], flags=re.S)
    old = 'uint32_t dex_code_item_offset_;' in fields
    expected = ('GcRoot<mirror::Class> declaring_class_;', 'std::atomic<std::uint32_t> access_flags_;',
                *((('uint32_t dex_code_item_offset_;',) if old else ())),
                'uint32_t dex_method_index_;', 'uint16_t method_index_;', 'uint16_t hotness_count_;',
                'void* data_;', 'void* entry_point_from_quick_compiled_code_;')
    positions = [fields.index(declaration) for declaration in expected]
    assert positions == sorted(positions), record['tag']
    assert old == (record['tag'] == 'android-11.0.0_r1')
    # Compile the actual fixed source field section, replacing only GcRoot with
    # its 32-bit compressed carrier. No ART object access or disassembly occurs.
    fields = fields.replace('GcRoot<mirror::Class>', 'uint32_t')
    size, index = (40, 12) if old else (32, 8)
    translation = '#include <atomic>\n#include <cstddef>\n#include <cstdint>\n' + \
        '#include "platform/common/art_method_metadata.h"\nstruct FixedSourceMethod {\n' + fields + '\n};\n' + \
        f'static_assert(sizeof(FixedSourceMethod)=={size});\n' + \
        f'static_assert(offsetof(FixedSourceMethod,dex_method_index_)=={index});\n' + \
        'static_assert(offsetof(FixedSourceMethod,access_flags_)==4);\n' + \
        f'static_assert(sizeof(artdcevm::method_metadata::{"Method11" if old else "Method12"})==sizeof(FixedSourceMethod));\n'
    path = out / (record['tag'] + '.cc')
    path.write_text(translation)
    compiler = args.ndk / 'toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android30-clang++'
    subprocess.run([str(compiler), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I',
                    str(root / 'artdcevm/src/main/cpp'), '-c', str(path), '-o', str(path.with_suffix('.o'))], check=True)
    report.append(dict(record, arm64_size=size, dex_method_index_offset=index,
                       atomic_access_flags_offset=4, no_device_identity_dispatch=True))
(out / 'audit.json').write_text(json.dumps(report, indent=2) + '\n')
print('Method identity source audit: eight fixed SHA-verified AOSP declarations compiled for ARM64')
