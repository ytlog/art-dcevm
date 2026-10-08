#!/usr/bin/env python3
"""Recheck fixed ART JNI scope declarations and ten captured named-symbol sets."""
import argparse
import hashlib
import json
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-dir', type=Path, default=Path('build/evidence/compatibility/20261008/native-access-source'))
parser.add_argument('--device-dir', type=Path, default=Path('build/evidence/compatibility/20261008'))
parser.add_argument('--output', type=Path)
args = parser.parse_args()

def body(text, declaration):
    match = re.search(declaration, text)
    if not match:
        raise AssertionError('Missing declaration: ' + declaration)
    start = text.index('{', match.end())
    depth = 1
    end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

sources = {}
source_records = []
for name in ('manifest.json', 'broker-manifest.json', 'check-jni-manifest.json'):
    for record in json.loads((args.source_dir / name).read_text()):
        assert re.fullmatch(r'[0-9a-f]{40}', record['commit'])
        assert record['url'] == ('https://android.googlesource.com/platform/art/+/' +
                                 record['commit'] + '/' + record['path'] + '?format=TEXT')
        data = (args.source_dir / record['tag'] / record['path']).read_bytes()
        assert hashlib.sha256(data).hexdigest() == record['sha256'], record
        sources[record['tag'], record['path']] = data.decode()
        source_records.append(record)

assert len(source_records) == 88
assert len({tag for tag, _ in sources}) == 8
report = {'source_records': source_records, 'source_families': [], 'device_symbols': []}
for tag in sorted({tag for tag, _ in sources}):
    jni = sources[tag, 'runtime/jni/jni_internal.cc']
    reflected = body(jni, r'static jmethodID FromReflectedMethod\(JNIEnv\* env, jobject \w+\)')
    scope = reflected.index('ScopedObjectAccess soa(env)')
    leaf = reflected.index('ArtMethod::FromReflectedMethod(soa,')
    assert scope < leaf and 'EncodeArtMethod' in reflected[:leaf]
    helper = body(sources[tag, 'runtime/art_method.cc'], r'ArtMethod\* ArtMethod::FromReflectedMethod\(const ScopedObjectAccessAlreadyRunnable& soa,\s*jobject \w+\)')
    assert 'soa.Decode<' in helper
    header = sources[tag, 'runtime/scoped_thread_state_change.h']
    assert 'class ScopedObjectAccessAlreadyRunnable : public ValueObject' in header
    assert 'class ScopedObjectAccess : public ScopedObjectAccessUnchecked' in header
    assert re.search(r'Thread\* const self_;.*?JNIEnvExt\* const env_;.*?JavaVMExt\* const vm_;', header, re.S)
    environment = sources[tag, 'runtime/jni/jni_env_ext.h']
    assert 'class JNIEnvExt : public JNIEnv' in environment
    assert re.search(r'Thread\* const self_;.*?JavaVMExt\* const vm_;', environment, re.S)
    change = sources[tag, 'runtime/scoped_thread_state_change-inl.h']
    transition = body(change, r'ScopedThreadStateChange::ScopedThreadStateChange\(Thread\* self, ThreadState new_thread_state\)')
    assert 'old_thread_state_ = self->GetState()' in transition
    assert re.search(r'if\s*\(old_thread_state_ != new_thread_state\)', transition)
    assert 'TransitionFromSuspendedToRunnable' in transition
    destructor = body(change, r'ScopedThreadStateChange::~ScopedThreadStateChange\(\)')
    assert 'old_thread_state_' in destructor and 'TransitionFromRunnableToSuspended' in destructor
    assert re.search(r'ScopedObjectAccessUnchecked::ScopedObjectAccessUnchecked\(JNIEnv\* env\).*?tsc_\(Self\(\), (?:ThreadState::)?kRunnable\)', change, re.S)
    plain = body(jni, r'static jobjectRefType GetObjectRefType\([^)]*\)')
    checked = body(sources[tag, 'runtime/jni/check_jni.cc'], r'static jobjectRefType GetObjectRefType\([^)]*\)')
    assert 'ScopedObjectAccess soa(env)' in checked
    assert 'GetObjectRefType(env,' in checked
    # CheckJNI nests a Runnable scope; old==new prevents any transition. Plain
    # JNI either only switches reference kind or enters the same nested scope.
    assert 'TransitionFrom' not in plain
    assert 'IndirectRefKind kind' in plain and 'switch (kind)' in plain
    assert 'return JNIGlobalRefType' in plain and 'return JNIWeakGlobalRefType' in plain
    assert 'ScopedObjectAccess' not in plain
    obj = sources[tag, 'runtime/obj_ptr.h']
    assert 'constexpr bool kObjPtrPoisoning = kIsDebugBuild' in obj
    assert 'uintptr_t reference_' in obj and 'reinterpret_cast<MirrorType*>(reference_)' in obj
    quick = sources[tag, 'runtime/entrypoints/quick/quick_jni_entrypoints.cc']
    version = int(tag.split('-')[1].split('.')[0])
    if version < 13:
        assert re.search(r'uint32_t JniMethodStart\(Thread\* self\)', quick)
        assert re.search(r'void JniMethodEnd\(uint32_t saved_local_ref_cookie, Thread\* self\)', quick)
        transition_kind = 'cookie JNI start/end: not void pure transitions'
    else:
        assert 'void artJniMethodStart(Thread* self)' in quick
        assert 'void artJniMethodEnd(Thread* self)' in quick
        transition_kind = 'void ART JNI transition pair'
    if version >= 15:
        assert 'std::is_trivially_copyable<ObjPtr<void>>' in obj
        glob = body(sources[tag, 'runtime/thread.cc'], r'ObjPtr<mirror::Object> Thread::DecodeGlobalJObject\(jobject obj\) const')
        assert 'DCHECK_NE(kind, kLocal)' in glob and 'vm_->DecodeGlobal(ref)' in glob
    report['source_families'].append({'tag': tag, 'transition': transition_kind,
        'scope_then_named_leaf_then_id_encode': True, 'nested_runnable_jni_compatible': True,
        'global_decode_release_no_poison_required': version >= 15})

leaf_name = '_ZN3art9ArtMethod19FromReflectedMethodERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobject'
global_name = '_ZNK3art6Thread19DecodeGlobalJObjectEP8_jobject'
selected = ('firebase-redfin-30-compressed', 'firebase-oriole-31', 'firebase-bluejay-32',
            'firebase-panther-33', 'firebase-b0q-33', 'firebase-shiba-34',
            'firebase-husky-35', 'firebase-frankel-36', 'firebase-cubs-37', 'local-collector-final')
for device in selected:
    paths = list((args.device_dir / device / 'libraries').glob('*libart.so/defined-symbols.json'))
    assert len(paths) == 1, (device, paths)
    raw = paths[0].read_bytes()
    symbols = set(json.loads(raw))
    assert leaf_name in symbols, device
    transitions = sorted(s for s in symbols if
        s.startswith(('artJniMethod', '_ZN3art6Thread', '_ZNK3art6Thread')) and
        ('Transition' in s or s in ('artJniMethodStart', 'artJniMethodEnd')) or
        re.match(r'_ZN3art\d+Scoped(?:ObjectAccess\w*|ThreadStateChange)[CD]\d', s))
    report['device_symbols'].append({'device': device, 'symbol_cache': str(paths[0]),
        'symbol_cache_sha256': hashlib.sha256(raw).hexdigest(),
        'reflected_leaf': True, 'global_decode': global_name in symbols,
        'transition_or_scope_entry_symbols': transitions})
report['limitation'] = ('Source order and symbol presence do not prove the JNI caller reaches the leaf after LTO. '
                       'A matched-device owned carrier probe must observe dispatch, Runnable state and lifetime '
                       'before capability publication. No hook is installed by this audit.')
encoded = json.dumps(report, indent=2) + '\n'
if args.output:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(encoded)
else:
    print(encoded)
print('Native access source audit: passed (eight fixed ART revisions, ten symbol inventories)')
