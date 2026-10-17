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
RUNTIME_TABLE = ABI / 'interface_runtime_table.h'


def read_runtime_table():
    table = RUNTIME_TABLE.read_text()
    entries = re.findall(
        r'\{(\d+),\s*(\d+),\s*(\d+),\s*&android(\d+)::(\w+),\s*'
        r'InterfaceRuntimeStatus::(kPreparedOnly|kDeviceVerified),\s*(?:true|false)\}', table)
    versions = [int(entry[0]) for entry in entries]
    declared = sorted(int(path.parent.name.removeprefix('android'))
                      for path in ABI.glob('android*/interface_runtime.h'))
    if versions != declared:
        raise ValueError('Interface runtime table must register every prepared declaration once')
    for release, first, last, namespace, name, status in entries:
        if release != namespace or int(first) > int(last):
            raise ValueError('Interface runtime table has an invalid release/API entry')
        if name not in ('kInterfaceRuntime', 'kPreparedInterfaceRuntime'):
            raise ValueError('Interface runtime table must reference a fixed runtime declaration')
    active = [int(release) for release, _, _, _, _, status in entries
              if status == 'kDeviceVerified']
    active_apis = [api for _, first, last, _, _, status in entries if status == 'kDeviceVerified'
                   for api in range(int(first), int(last) + 1)]
    if active != [13, 14, 15, 16] or active_apis != [33, 34, 35, 36]:
        raise ValueError('Unverified source preparation was added to runtime selection')
    return entries, versions, active


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-lib', type=Path, default=ROOT / 'assets/runtime_lib')
    parser.add_argument('--evidence-root', type=Path, default=ROOT / 'build/evidence/interface-abi')
    args = parser.parse_args()
    if not args.evidence_root.resolve().is_relative_to((ROOT / 'build/evidence').resolve()):
        parser.error('Evidence must stay under ignored build/evidence')
    out = new_evidence_directory(args.evidence_root.resolve())
    entries, versions, active = read_runtime_table()
    properties = dict(line.split('=', 1) for line in (ROOT / 'local.properties').read_text().splitlines()
                      if '=' in line and not line.startswith('#'))
    prebuilt = Path(properties['sdk.dir']) / 'ndk/27.2.12479018/toolchains/llvm/prebuilt'
    compiler = next(path / 'bin/aarch64-linux-android30-clang++' for path in prebuilt.iterdir()
                    if (path / 'bin/aarch64-linux-android30-clang++').is_file())
    lines = ['#include "platform/abi/arm64/interface_runtime_table.h"']
    lines.append('#include "platform/abi/arm64/runtime_thread_table.h"')
    lines.append('using namespace artdcevm::abi::arm64;')
    expected = {11: (16, 52, 216, 296, 12), 12: (12, 48, 208, 288, 12),
                13: (12, 36, 200, 280, 12), 14: (12, 36, 208, 288, 12),
                15: (12, 32, 200, 280, 16), 16: (12, 32, 200, 280, 16),
                17: (12, 32, 200, 280, 16)}
    for index, (release, _, _, _, name, status) in enumerate(entries):
        v = int(release)
        offsets = expected[v]
        profile = f'kInterfaceRuntimes[{index}]'
        lines.append(f'static_assert({profile}.runtime == &android{v}::{name});')
        lines.append(f'static_assert(UsesLinkedIfTable({v}) == {str(v in (15, 16)).lower()});')
        selected = f'{profile}.runtime' if status == 'kDeviceVerified' else 'nullptr'
        lines.append(f'static_assert(RuntimeFor({v}) == {selected});')
        for member, value in zip(('tid', 'is_gc_marking', 'jni_env', 'top_handle_scope'), offsets[:4]):
            lines.append(f'static_assert(RuntimeThreadFor<android{v}::Thread>().{member} == {value});')
        lines.append(f'static_assert({profile}.runtime->scope_header_size == {offsets[4]});')
        lines.append(f'static_assert({profile}.runtime->redefinition_size == 72);')
        lines.append(f'static_assert({profile}.runtime->scope_thread_id_signed == {str(v == 11).lower()});')
        lines.append(f'static_assert(kMaxInterfaceScopeHeader >= {profile}.runtime->scope_header_size);')
    lines.append('static_assert(kMaxInterfaceScopeHeader == 16);')
    for release in (-1, 0, 10, 18, 99):
        lines.append(f'static_assert(RuntimeFor({release}) == nullptr);')
    for api in range(-1, 41):
        lines.append(f'static_assert(IsDeviceVerifiedInterfaceApi({api}) == '
                     f'{str(api in (33, 34, 35, 36)).lower()});')
    lines.append('static_assert(android17::Class::DecodeClinitThreadId(123) == 123);')
    lines.append('static_assert(android17::Class::DecodeClinitThreadId(0xff000123) == 0);')
    source = out / 'profiles.cc'
    source.write_text('\n'.join(lines) + '\n')
    result = subprocess.run([str(compiler), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                             '-fsyntax-only', '-I', str(CPP), str(source)], capture_output=True, text=True)
    (out / 'compiler-raw.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        raise ValueError('Prepared profile compilation failed; see ignored compiler evidence')
    engine = '\n'.join((CPP / 'platform/common' / name).read_text() for name in (
        'interface_redefinition.cc', 'interface_admission.cc', 'interface_group_snapshot.cc',
        'interface_allocation.cc', 'interface_redefinition_internal.h'))
    if re.search(r'#include\s+"[^"\n]*/android\d+/', engine) or re.search(r'android\d+::', engine):
        raise ValueError('Shared interface implementation must not reference version-specific types')

    spec = importlib.util.spec_from_file_location('symbol_audit', ROOT / 'tools/check-structural-symbols.py')
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    catalog, catalog_hash = audit.read_catalog(SYMBOLS / 'interface_redefinition_symbols.h')
    extra, extra_hash = audit.read_catalog(SYMBOLS / 'interface_abi_preparation_symbols.h')
    shared, shared_hash, _ = audit.read_active_catalog(SYMBOLS / 'runtime_thread_symbols.h',
        CPP / 'platform/common/runtime_thread.cc', 'Runtime thread')
    combined_hash = hashlib.sha256((catalog_hash + extra_hash + shared_hash).encode('ascii')).hexdigest()
    report = audit.collect_report(args.runtime_lib.resolve(), catalog + extra + shared, combined_hash)
    alternatives = [('kCanNormal', 'kGetNormal'), ('kCanStructural', 'kGetStructural'),
                    ('kLookupClassChars', 'kLookupClassView')]
    optional = {name for group in alternatives for name in group}
    optional.update(('kHandleScopeVisitRoots', 'kHandleScopeVisitRootsSigned'))
    # Only abstract additions on ART 13/14 need this early link-error service.
    # Its absence must not make unrelated interface services look unavailable.
    non_public = ('kThrowNonPublicGlobal', 'kThrowNonPublicLocal', 'kThrowNonPublicUnique')
    optional.update(non_public)
    optional.add('kUseReadBarrier')
    legacy_allocators = ('kObjectArrayAlloc', 'kClassArrayAlloc',
                         'kObjectArrayAllocWithAllocator', 'kClassArrayAllocWithAllocator')
    optional.update(legacy_allocators)
    rows = {}
    for runtime in report['sets']:
        api = int(runtime['label'].split('/')[0].split('-')[1])
        entries = {entry['id']: entry['present'] for entry in runtime['catalog']}
        runtime['nonPublicImplementationLinkError'] = any(entries.get(name) for name in non_public)
        runtime['arrayAllocation'] = {
            'strategy': 'Native JNI arrays held by global references',
            'decodeGlobalAvailable': entries['kDecodeGlobal'],
            'historicalWrappers': [name for name in legacy_allocators if entries.get(name)],
            'allocatorSelectionRequired': False}
        missing = [name for name in entries if name not in optional and not entries[name]]
        missing += ['/'.join(group) for group in alternatives if not any(entries.get(name) for name in group)]
        scope_service = 'kHandleScopeVisitRootsSigned' if api == 30 else 'kHandleScopeVisitRoots'
        runtime['handleScopeTraversal'] = {
            'threadIdType': 'pid_t/int32_t' if api == 30 else 'uint32_t',
            'requiredService': scope_service, 'available': entries[scope_service]}
        if not entries[scope_service]:
            missing.append(scope_service)
        runtime['sourcePreparationMissingServices'] = missing
        row = rows.setdefault(api, {'sets': 0, 'coveredSets': 0})
        row['sets'] += 1
        row['coveredSets'] += not missing
    report['sourcePreparation'] = {
        'compiledVersions': versions, 'activeRuntimeCandidates': active,
        'apiCoverage': rows, 'scope': 'Source declarations and named-table coverage only; no device ABI proof',
        'headerSha256': {str(p.relative_to(CPP)): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in sorted(ABI.rglob('*.h'))}}
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps({'compiledVersions': versions, 'activeRuntimeCandidates': active,
                      'apiCoverage': rows}, indent=2))
    print('Evidence: ' + str(out.relative_to(ROOT)))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, KeyError, StopIteration):
        print('Unable to validate prepared declarations; inspect ignored evidence', file=sys.stderr)
        sys.exit(1)
