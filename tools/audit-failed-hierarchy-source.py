#!/usr/bin/env python3
"""Recheck the failed-class lifetime and named Handle/status ABI from fixed ART."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-dir', type=Path, default=Path('build/evidence/compatibility/20261008/failed-hierarchy-retirement-source'))
parser.add_argument('--arm64-cxx', required=True)
args = parser.parse_args()
sources = {}
for record in json.loads((args.source_dir / 'manifest.json').read_text()):
    assert record['commit'] == '88baac34da072b41917b4adc1987c55546266b33'
    assert record['url'] == f"https://android.googlesource.com/platform/art/+/{record['commit']}/{record['path']}?format=TEXT"
    data = (args.source_dir / record['path']).read_bytes()
    assert hashlib.sha256(data).hexdigest() == record['sha256']
    sources[record['path']] = data.decode()
redefine = sources['openjdkjvmti/ti_redefine.cc']
assert 'obj->IsClass() && obj->AsClass()->IsResolved()' in redefine
assert 'old_klass->IsAssignableFrom(obj->AsClass())' in redefine
assert 'heap->VisitObjects(' in redefine and 'dex_file_.release()' in redefine
allocate = redefine[redefine.index('  // This is a stripped down DefineClass.'):redefine.index('void Redefiner::ClassRedefinition::UnregisterJvmtiBreakpoints')]
assert allocate.index('LinkClass(') < allocate.index('ForceClassInitialized(') < allocate.index('EnsureInstanceFieldIds(')
assert 'return nullptr;' in allocate[allocate.index('EnsureInstanceFieldIds('):]
klass = sources['runtime/mirror/class.cc']
assert 'new_status != ClassStatus::kRetired' in klass
assert 'CHECK_EQ(new_status, ClassStatus::kVisiblyInitialized)' in klass
assert 'CHECK(!Class::IsErroneous(new_status))' in klass
assert 'CHECK(thiz->IsResolved())' in klass
linker = sources['runtime/class_linker.cc']
force = linker[linker.index('void ClassLinker::ForceClassInitialized('):linker.index('ClassLinker::VisiblyInitializedCallback* ClassLinker::MarkClassInitialized')]
assert 'MakeInitializedClassesVisiblyInitialized(self, /*wait=*/true)' in force
header = sources['runtime/mirror/class.h']
assert 'status >= ClassStatus::kResolved || status == ClassStatus::kErrorResolved' in header
handle = sources['runtime/handle.h']
main_handle = handle[handle.index('class Handle : public ValueObject {'):handle.index('class MutableHandle : public Handle<T>')]
assert 'StackReference<mirror::Object>* reference_;' in main_handle
assert 'Handle(const Handle<T>& handle) = default;' in main_handle
assert '~Handle(' not in main_handle and 'virtual ' not in main_handle
value_object = sources['libartbase/base/value_object.h']
assert re.search(r'class ValueObject\s*{\s*private:\s*DISALLOW_ALLOCATION\(\);\s*};', value_object)
status = re.search(r'enum class ClassStatus : uint8_t\s*{.*?};', sources['runtime/class_status.h'], re.S).group()
ensure = re.search(r'static bool EnsureInstanceFieldIds\(Handle<Class> h_this\)', header).group()
out = Path('build/evidence/host/failed-hierarchy-source')
out.mkdir(parents=True, exist_ok=True)
# All instance data, base and special members are retained from the source.
# Member-function bodies and static allocation restrictions do not affect ABI.
copy = re.search(r'Handle\(const Handle<T>& handle\) = default;', main_handle).group()
field = re.search(r'StackReference<mirror::Object>\* reference_;', main_handle).group()
unit = out / 'literal-handle-status.cc'
unit.write_text('''#include <cstdint>
#include <type_traits>
namespace art {
class ValueObject {};
namespace mirror {class Object;}
template<class T> class StackReference;
template<class T> class Handle : public ValueObject { public:
''' + copy + '\n' + field + '\n};\n' + status + '''
namespace mirror {class Class {public:
''' + ensure + ''';
void SetStatusLocked(ClassStatus);
};}
}
static_assert(sizeof(art::Handle<art::mirror::Class>)==8);
static_assert(alignof(art::Handle<art::mirror::Class>)==8);
static_assert(std::is_trivially_copyable_v<art::Handle<art::mirror::Class>>);
static_assert(static_cast<uint8_t>(art::ClassStatus::kRetired)==1);
extern "C" bool SourceEnsure(art::Handle<art::mirror::Class> handle) {
  return art::mirror::Class::EnsureInstanceFieldIds(handle);
}
extern "C" bool BridgeEnsure(bool (*fn)(const void*), const void* slot) { return fn(slot); }
extern "C" void SourceRetire(art::mirror::Class* klass) { klass->SetStatusLocked(art::ClassStatus::kRetired); }
extern "C" void BridgeRetire(void (*fn)(void*,uint8_t),void* klass) { fn(klass,1); }
''')
ir = out / 'literal-handle-status.ll'
command = [args.arm64_cxx, '--target=aarch64-linux-android30', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', '-S', '-emit-llvm', str(unit), '-o', str(ir)]
result = subprocess.run(command, capture_output=True, text=True)
(out / 'compile.log').write_text(result.stdout + result.stderr)
result.check_returncode()
text = ir.read_text()
for name in ('SourceEnsure', 'BridgeEnsure'):
    body = re.search(r'define[^\n]*@' + name + r'\(.*?\n}', text, re.S).group()
    assert 'sret(' not in body and 'byval(' not in body and 'call' in body
for name in ('SourceRetire', 'BridgeRetire'):
    body = re.search(r'define[^\n]*@' + name + r'\(.*?\n}', text, re.S).group()
    assert re.search(r'i8(?:[^,)]*?) 1\)', body)
(out / 'audit.json').write_text(json.dumps({'fixed_source_lifetime_checked': True,
    'handle': 'one trivial pointer-sized direct argument; borrowed StackReference slot',
    'status': 'uint8_t Retired1; exclude status14; exclusive mutator and resolved old class',
    'command': command, 'runtime_proof': 'separate owned host and device regressions required'}, indent=2) + '\n')
print('Failed hierarchy source and ARM64 Handle/status ABI: passed')
