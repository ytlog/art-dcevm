#!/usr/bin/env python3
"""Pin official AOSP ART sources and report declaration changes, not runtime compatibility."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
TAGS = [f'android-{v}.0.0_r1' for v in range(11, 18)]
TAGS.insert(2, 'android-12.1.0_r1')
BASE = 'https://android.googlesource.com/platform/art/'
INTERFACES = {
    'runtime/thread_list.h': ('SuspendThreadByPeer',),
    'runtime/thread.h': ('HandleScopeVisitRoots',),
    'runtime/instrumentation.h': ('GetCodeForInvoke', 'InitializeMethodsCode', 'InstrumentThreadStack'),
    'runtime/jit/jit.h': ('CompileMethod',),
    'runtime/jit/jit_code_cache.h': ('VisitAllMethods',),
    'runtime/mirror/class.h': ('GetDirectInterface', 'CanAccessMember', 'CheckResolvedMethodAccess'),
    'runtime/mirror/var_handle.h': ('GetAccessModeTemplate',),
    'runtime/art_field.h': ('GetName', 'GetTypeDescriptor'),
    'compiler/optimizing/inliner.h': ('TryInline',),
    'runtime/class_linker.h': ('ResolveMethod', 'ResolveMethodId', 'ResolveMethodWithChecks',
                              'ThrowIfInvokeClassMismatch'),
    'runtime/class_linker-inl.h': ('ResolveMethod', 'ResolveMethodId', 'ResolveMethodWithChecks',
                                  'CheckInvokeClassMismatch', 'ThrowIfInvokeClassMismatch'),
    'runtime/class_linker.cc': ('ResolveMethod', 'ResolveMethodId', 'ResolveMethodWithoutInvokeType',
                              'DeleteClassLoader'),
    'runtime/linear_alloc.h': ('ContainsUnsafe',),
    'runtime/linear_alloc.cc': ('ContainsUnsafe',),
    'runtime/linear_alloc-inl.h': ('ContainsUnsafe', 'Contains', 'Alloc'),
    'libartbase/base/arena_allocator.h': ('Contains', 'GetArenaPool'),
    'libartbase/base/arena_allocator.cc': ('Contains',),
    'runtime/base/mutex.h': ('Mutex',),
    'runtime/base/atomic_pair.h': ('AtomicPairLoadAcquire',),
    'runtime/mirror/dex_cache.h': ('GetResolvedMethod',),
    'runtime/mirror/dex_cache-inl.h': ('GetResolvedMethod',),
    'runtime/mirror/class-inl.h': ('ResolvedMethodAccessTest', 'CanAccessMember'),
    'runtime/common_throws.h': ('ThrowIllegalAccessErrorClassForMethodDispatch',
                                'ThrowIllegalAccessErrorMethod', 'ThrowIncompatibleClassChangeError'),
    'libdexfile/dex/standard_dex_file.cc': ('SupportsDefaultMethods',),
    'libdexfile/dex/compact_dex_file.cc': ('SupportsDefaultMethods',),
    'libdexfile/dex/dex_file.h': ('SupportsDefaultMethods',),
    'libdexfile/dex/compact_dex_file.h': ('GetFeatureFlags',),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=ROOT / 'build/evidence/aosp-compatibility')
    parser.add_argument('--proxy', help='Optional HTTPS proxy for curl')
    parser.add_argument('--offline', action='store_true', help='Require previously verified cached sources')
    parser.add_argument('--tag', action='append',
                        help='Explicit official release tag; repeat to audit device-matched builds')
    args = parser.parse_args()
    tags = args.tag or TAGS
    if any(not re.fullmatch(r'android-\d+\.\d+\.\d+_r\d+', tag) for tag in tags):
        parser.error('--tag must be an Android release tag such as android-11.0.0_r46')
    if len(set(tags)) != len(tags):
        parser.error('Duplicate --tag values')
    args.source_dir.mkdir(parents=True, exist_ok=True)
    refs_path = args.source_dir / 'refs.json'
    manifest_path = args.source_dir / 'source-manifest.json'
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else []
    cached = {(x['tag'], x['path']): x for x in manifest}

    def download(url):
        command = ['curl', '-fsSL', '--retry', '2', '--connect-timeout', '10', '--max-time', '45']
        if args.proxy:
            command += ['--proxy', args.proxy]
        result = subprocess.run(command + [url], capture_output=True, check=True)
        return result.stdout

    if not refs_path.exists():
        if args.offline:
            parser.error('Offline audit requires cached refs.json')
        refs_path.write_bytes(download(BASE + '+refs?format=JSON'))
    refs = json.loads(refs_path.read_bytes()[4:])
    sources = []
    versions = []
    errors = []
    for tag in tags:
        try:
            ref = refs['refs/tags/' + tag]
            commit = ref.get('peeled', ref['value'])
        except KeyError:
            errors.append(f'Missing official release tag: {tag}')
            continue
        major = int(tag.split('-')[1].split('.')[0])
        oat = 'runtime/oat/' if major >= 15 else 'runtime/'
        # Android 17 removed CompactDex from the official libdexfile tree.
        compact_sources = {'libdexfile/dex/compact_dex_file.h', 'libdexfile/dex/compact_dex_file.cc'}
        files = ['openjdkjvmti/ti_redefine.h', 'openjdkjvmti/ti_redefine.cc',
                 'openjdkjvmti/ti_extension.cc', 'runtime/art_method.h',
                 oat + 'oat_quick_method_header.h', oat + 'stack_map.h', 'runtime/Android.bp',
                 *(name for name in INTERFACES
                   if name != ('runtime/linear_alloc.cc' if major >= 14 else 'runtime/linear_alloc-inl.h')
                   and (major < 17 or name not in compact_sources)
                   and (major >= 13 or name != 'runtime/base/atomic_pair.h'))]
        content = {}
        for name in files:
            path = args.source_dir / tag / name
            url = BASE + f'+/{commit}/{name}?format=TEXT'
            record = {'tag': tag, 'commit': commit, 'path': name, 'url': url, 'ok': False}
            old = cached.get((tag, name), {})
            try:
                valid = (old.get('ok') and old.get('commit') == commit and path.is_file() and
                         old.get('sha256') == hashlib.sha256(path.read_bytes()).hexdigest())
                if not valid:
                    if args.offline:
                        raise ValueError('No verified cached source')
                    data = base64.b64decode(download(url), validate=True)
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)
                    time.sleep(1)
                content[name] = path.read_text()
                record.update(ok=True, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            except (OSError, ValueError, subprocess.CalledProcessError) as error:
                record['error'] = str(error)
                errors.append(f'{tag}/{name}: {error}')
            sources.append(record)
        if len(content) != len(files):
            continue
        header = content[oat + 'oat_quick_method_header.h']
        maps = content[oat + 'stack_map.h']
        redefine = content['openjdkjvmti/ti_redefine.h']
        implementation = content['openjdkjvmti/ti_redefine.cc']
        extensions = content['openjdkjvmti/ti_extension.cc']
        declarations = {}
        # Preserve source excerpts for review; do not infer object offsets or ABI compatibility.
        for name, methods in INTERFACES.items():
            if name not in content:
                continue
            lines = content[name].splitlines()
            for method in methods:
                matches = []
                for index, line in enumerate(lines):
                    if re.search(r'\b' + method + r'\s*\(', line) and not line.lstrip().startswith('//'):
                        excerpt = []
                        for following in lines[index:index + 12]:
                            excerpt.append(following.strip())
                            if ';' in following or '{' in following:
                                break
                        matches.append({'line': index + 1, 'text': ' '.join(excerpt)})
                declarations[name + ':' + method] = matches
        checked_referrer = re.search(
            r'inline ArtMethod\* ClassLinker::ResolveMethodWithChecks\(.*?(?=\ninline |\Z)',
            content['runtime/class_linker-inl.h'], re.DOTALL)
        checked_referrer_order = None
        if checked_referrer:
            body = checked_referrer.group(0)
            operations = ('ResolveType', 'ThrowIfInvokeClassMismatch', 'GetResolvedMethod',
                          'FindResolvedMethod', 'CanAccessMember', 'CheckIncompatibleClassChange',
                          'FindIncompatibleMethod', 'ThrowNoSuchMethodError')
            positions = [(body.find(name + '('), name) for name in operations]
            checked_referrer_order = [name for position, name in sorted(positions) if position >= 0]
        versions.append({
            'tag': tag, 'commit': commit,
            'artMethodHasDexCodeItemOffset': 'uint32_t dex_code_item_offset_' in content['runtime/art_method.h'],
            'classRedefinitionHasLockAcquired': 'bool lock_acquired_' in redefine,
            'redefinitionUsesBlockingMutex': 'std::lock_guard<std::mutex> lg(redefinition_lock)' in implementation,
            'quickHeaderWords': re.findall(r'uint32_t\s+(vmap_table_offset_|code_size_|data_|code_info_offset_)\s*=', header),
            'codeInfoHeaderCount': re.search(r'kNumHeaders\s*=\s*(\d+)', maps).group(1),
            'codeInfoFlags': re.findall(r'(kHasInlineInfo|kHasShouldDeoptimizeFlag|kIsBaseline|kIsDebuggable|kIsFast)\s*=\s*1\s*<<\s*(\d+)', maps),
            'structuralExtensionDeclared': all(name in extensions for name in (
                'com.android.art.class.structurally_redefine_classes',
                'com.android.art.class.is_structurally_modifiable_class')),
            'interfaceDeclarationExcerpts': declarations,
            'linearAllocatorImplementationFile': 'runtime/linear_alloc-inl.h' if major >= 14 else 'runtime/linear_alloc.cc',
            'linearAllocatorHasAllocationKind': 'LinearAllocKind' in content['runtime/linear_alloc.h'],
            'compilerBundledIntoArt': bool(re.search(
                r'name:\s*"libart",(?:(?!\n\}).)*?whole_static_libs:\s*\[[^]]*"libart-compiler"',
                content['runtime/Android.bp'], re.DOTALL)),
            'defaultMethodsDexVersionDeclaration': re.findall(
                r'kDefaultMethodsVersion\s*=\s*(\d+)', content['libdexfile/dex/dex_file.h']),
            'compactDexDefaultMethodsFeatureDeclaration': re.findall(
                r'kDefaultMethods\s*=\s*(0x[0-9a-fA-F]+|\d+)',
                content.get('libdexfile/dex/compact_dex_file.h', '')),
            'dexCachePairSynchronization': ('sequence-lock'
                if 'kSeqLock' in content.get('runtime/base/atomic_pair.h', '')
                else 'whole-pair-atomic'),
            'dexCachePairHasKeyBeforeValue': ('IntType key;' in
                content.get('runtime/base/atomic_pair.h', '')),
            'dexCacheMethodStorageFamily': ('dual-full-or-pair'
                if 'resolved_methods_array_' in content['runtime/mirror/dex_cache.h'] else 'pair'),
            'dexCacheNativeMethodLoadRelaxed': ('entries_[index].load(std::memory_order_relaxed)' in
                content['runtime/mirror/dex_cache.h']),
            'dexCacheMethodSlotCount': re.findall(
                r'kDexCacheMethodCacheSize\s*=\s*(\d+)', content['runtime/mirror/dex_cache.h']),
            'dexFileMappingSizeSource': ('header-file-size'
                if 'size_t unused_size_' in content['libdexfile/dex/dex_file.h']
                else 'stored-size'),
            'dexFileUsesDataArrayRef': bool(re.search(
                r'ArrayRef<const uint8_t>\s+const data_', content['libdexfile/dex/dex_file.h'])),
            'dexFileSizeGetterBody': re.findall(
                r'size_t Size\(\) const \{ return (header_->file_size_|size_); \}',
                content['libdexfile/dex/dex_file.h']),
            'compactDexImplementationFile': 'libdexfile/dex/compact_dex_file.cc' if major < 17 else None,
            'compactDexDefaultMethodsReadsFeatureFlags': ('GetHeader().GetFeatureFlags()' in
                content['libdexfile/dex/compact_dex_file.cc']) if major < 17 else None,
            'checkedReferrerResolverOperationOrder': checked_referrer_order,
            'methodResolverDeclarationFamily': ('separate-id-and-checks'
                if re.search(r'\bResolveMethodWithChecks\s*\(', content['runtime/class_linker.h'])
                else 'templated-resolve-mode'),
        })
    # Preserve unrelated previously fetched sources and their provenance.
    merged = dict(cached)
    merged.update({(x['tag'], x['path']): x for x in sources})
    manifest_path.write_text(json.dumps(list(merged.values()), indent=2) + '\n')
    report = {'scope': 'AOSP source declarations only; no device compatibility claim',
              'complete': not errors and len(versions) == len(tags),
              'versions': versions, 'errors': errors}
    (args.source_dir / 'audit.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['complete'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
