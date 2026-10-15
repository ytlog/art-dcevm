#!/usr/bin/env python3
"""Compile source-only arm64 interface profiles and inventory named services.

Never executes target code, disassembles libraries or enables runtime profiles.
Detailed reports and compiler output remain under ignored build/evidence.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / 'artdcevm/src/main/cpp'
ABI = CPP / 'platform/abi/arm64'
SYMBOLS = CPP / 'platform/symbols'
VERSIONS = (11, 12, 13, 14, 15, 16, 17)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-lib', type=Path, default=ROOT / 'assets/runtime_lib')
    parser.add_argument('--evidence-root', type=Path, default=ROOT / 'build/evidence/interface-abi')
    args = parser.parse_args()
    if not args.evidence_root.resolve().is_relative_to((ROOT / 'build/evidence').resolve()):
        parser.error('Evidence must stay under ignored build/evidence')
    out = new_evidence_directory(args.evidence_root.resolve())
    properties = dict(line.split('=', 1) for line in (ROOT / 'local.properties').read_text().splitlines()
                      if '=' in line and not line.startswith('#'))
    prebuilt = Path(properties['sdk.dir']) / 'ndk/27.2.12479018/toolchains/llvm/prebuilt'
    compiler = next(path / 'bin/aarch64-linux-android30-clang++' for path in prebuilt.iterdir()
                    if (path / 'bin/aarch64-linux-android30-clang++').is_file())
    lines = [f'#include "platform/abi/arm64/android{v}/interface_runtime.h"' for v in VERSIONS]
    lines.append('using namespace artdcevm::abi::arm64;')
    expected = {11: (16, 52, 216, 296, 12), 12: (12, 48, 208, 288, 12),
                13: (12, 36, 200, 280, 12), 14: (12, 36, 208, 288, 12),
                15: (12, 32, 200, 280, 16), 16: (12, 32, 200, 280, 16),
                17: (12, 32, 200, 280, 16)}
    for v, offsets in expected.items():
        name = 'kInterfaceRuntime' if v in (13, 16) else 'kPreparedInterfaceRuntime'
        for member, value in zip(('tid', 'is_gc_marking', 'jni_env', 'top_handle_scope', 'scope_header_size'), offsets):
            lines.append(f'static_assert(android{v}::{name}.{member} == {value});')
        lines.append(f'static_assert(android{v}::{name}.redefinition_size == 72);')
    lines.append('static_assert(android17::Class::DecodeClinitThreadId(123) == 123);')
    lines.append('static_assert(android17::Class::DecodeClinitThreadId(0xff000123) == 0);')
    source = out / 'profiles.cc'
    source.write_text('\n'.join(lines) + '\n')
    result = subprocess.run([str(compiler), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                             '-fsyntax-only', '-I', str(CPP), str(source)], capture_output=True, text=True)
    (out / 'compiler-raw.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        raise ValueError('Prepared profile compilation failed; see ignored compiler evidence')
    engine = (CPP / 'platform/common/interface_redefinition.cc').read_text()
    selection = engine[engine.index('const InterfaceRuntime* RuntimeFor('):engine.index('void Disable(')]
    active = sorted(map(int, re.findall(r'release == (\d+)', selection)))
    if active != [13, 16] or 'kPreparedInterfaceRuntime' in engine:
        raise ValueError('Unverified source preparation was added to runtime selection')

    spec = importlib.util.spec_from_file_location('symbol_audit', ROOT / 'tools/check-structural-symbols.py')
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    catalog, catalog_hash = audit.read_catalog(SYMBOLS / 'interface_redefinition_symbols.h')
    extra, extra_hash = audit.read_catalog(SYMBOLS / 'interface_abi_preparation_symbols.h')
    combined_hash = hashlib.sha256((catalog_hash + extra_hash).encode('ascii')).hexdigest()
    report = audit.collect_report(args.runtime_lib.resolve(), catalog + extra, combined_hash)
    alternatives = [('kCanNormal', 'kGetNormal'), ('kCanStructural', 'kGetStructural'),
                    ('kLookupClassChars', 'kLookupClassView'),
                    ('kObjectArrayAlloc', 'kClassArrayAlloc', 'kObjectArrayAllocWithAllocator',
                     'kClassArrayAllocWithAllocator'),
                    ('kHandleScopeVisitRoots', 'kHandleScopeVisitRootsSigned')]
    optional = {name for group in alternatives for name in group}
    rows = {}
    for runtime in report['sets']:
        api = int(runtime['label'].split('/')[0].split('-')[1])
        entries = {entry['id']: entry['present'] for entry in runtime['catalog']}
        missing = [name for name in entries if name not in optional and not entries[name]]
        missing += ['/'.join(group) for group in alternatives if not any(entries.get(name) for name in group)]
        runtime['sourcePreparationMissingServices'] = missing
        row = rows.setdefault(api, {'sets': 0, 'coveredSets': 0})
        row['sets'] += 1
        row['coveredSets'] += not missing
    report['sourcePreparation'] = {
        'compiledVersions': list(VERSIONS), 'activeRuntimeCandidates': active,
        'apiCoverage': rows, 'scope': 'Source declarations and named-table coverage only; no device ABI proof',
        'headerSha256': {str(p.relative_to(CPP)): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in sorted(ABI.rglob('*.h'))}}
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps({'compiledVersions': list(VERSIONS), 'activeRuntimeCandidates': active,
                      'apiCoverage': rows}, indent=2))
    print('Evidence: ' + str(out.relative_to(ROOT)))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, KeyError, StopIteration):
        print('Unable to validate prepared declarations; inspect ignored evidence', file=sys.stderr)
        sys.exit(1)
