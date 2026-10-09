#!/usr/bin/env python3
"""Exercise unique ELF name parsing on malformed metadata and real device SOs."""
import argparse
import hashlib
import json
import lzma
from xz_host_objects import compile_xz
from elf_mini_inventory import mini_symbol_inventory
import os
import re
from pathlib import Path
import struct
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('manifests', nargs='*', type=Path)
parser.add_argument('--shadow-fixture', type=Path, help='Pixel 7 or Samsung 13 libart ELF for HookEngine integration')
args = parser.parse_args()
out = root / 'build/evidence/host/elf-symbol-names'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'test'
cpp = root / 'artdcevm/src/main/cpp'
xz_objects = compile_xz(root, out)
subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-O1', '-g',
                '-DXZ_USE_CRC64', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-fno-omit-frame-pointer', '-I', str(cpp),
                str(root / 'tests/host/elf_symbol_names_test.cc'),
                str(cpp / 'support/elf_symbol_names.cc'), *xz_objects, '-o', str(binary)], check=True)


def run(path, expected=None):
    result = subprocess.run([str(binary), str(path)] + ([expected] if expected else []), capture_output=True, text=True,
                            env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
    result.check_returncode()
    return dict(line.split('\t') for line in result.stdout.splitlines())


# Standard ELF64 section/name metadata with deliberately meaningless st_value:
# the parser must only return names, including when executable bytes do not exist.
strings = b'\0'
records = b'\0' * 24
for name, binding, section in [('_Zlocal.__uniq.123', 0, 1),
                               ('_Zlto.__uniq.77.llvm.88', 0, 1),
                               ('_Zambiguous.__uniq.1.llvm.2', 0, 1),
                               ('_ZbadSeparator.__uniq.1.llvm88', 0, 1),
                               ('_ZbadDigit.__uniq.1.llvm.a', 0, 1),
                               ('_ZemptyLto.__uniq.1.llvm.', 0, 1),
                               ('_ZemptyFirst.__uniq..llvm.2', 0, 1),
                               ('_ZdoubleLto.__uniq.1.llvm.2.llvm.3', 0, 1),
                               ('_ZdoubleUnique.__uniq.1.__uniq.2', 0, 1),
                               ('_Zambiguous.__uniq.1', 0, 1),
                               ('_Zambiguous.__uniq.2', 0, 1),
                               ('_Zglobal.__uniq.4', 1, 1),
                               ('_Zundefined.__uniq.5', 0, 0),
                               ('_Ztext.__uniq.bad', 0, 1),
                               ('_Zempty.__uniq.', 0, 1)]:
    records += struct.pack('<IBBHQQ', len(strings), binding * 16 + 2, 0, section, 0xffffffffffffffff, 0)
    strings += name.encode() + b'\0'
header = struct.pack('<16sHHIQQQIHHHHHH', b'\x7fELF\x02\x01\x01' + b'\0' * 9,
                     3, 183, 1, 0, 0, 64, 0, 64, 0, 0, 64, 3, 0)
sections = b'\0' * 64
sections += struct.pack('<IIQQQQIIQQ', 0, 2, 0, 0, 256, len(records), 2, 0, 8, 24)
sections += struct.pack('<IIQQQQIIQQ', 0, 3, 0, 0, 256 + len(records), len(strings), 0, 0, 1, 0)
data = header + sections + records + strings
fixture = out / 'names.elf'
fixture.write_bytes(data)
assert run(fixture) == {'_Zlocal': '_Zlocal.__uniq.123', '_Zlto': '_Zlto.__uniq.77.llvm.88'}
assert run(out / 'nonexistent.elf') == {}
negative_count = 0
for length in (0, 10, 63, 64, 255, len(data) - 1):
    fixture.write_bytes(data[:length])
    assert run(fixture, "reject") == {}
    negative_count += 1
for offset, value in [(4, b'\x01'), (5, b'\x02'), (18, b'\x3e\0'),
                      (40, b'\xff' * 8), (64 + 64 + 40, b'\xff' * 4),
                      (64 + 64 + 56, b'\x01' + b'\0' * 7)]:
    damaged = bytearray(data)
    damaged[offset:offset + len(value)] = value
    fixture.write_bytes(damaged)
    assert run(fixture, "reject") == {}
    negative_count += 1

# Real XZ-format streams wrap ELF symbol metadata; C++ parser/decoder operates
# on complete files. No runtime SO is checked in, and st_value is meaningless.
def compressed_elf(payload):
    names = b'\0.shstrtab\0.gnu_debugdata\0'
    h = struct.pack('<16sHHIQQQIHHHHHH', b'\x7fELF\x02\x01\x01' + b'\0' * 9,
                    3, 183, 1, 0, 0, 64, 0, 64, 0, 0, 64, 3, 1)
    sec = b'\0' * 64
    sec += struct.pack('<IIQQQQIIQQ', 1, 3, 0, 0, 256, len(names), 0, 0, 1, 0)
    sec += struct.pack('<IIQQQQIIQQ', 11, 1, 0, 0, 256 + len(names), len(payload), 0, 0, 1, 0)
    return h + sec + names + payload

compressed_cases = 0
for check in (lzma.CHECK_CRC32, lzma.CHECK_CRC64):
    stream = lzma.compress(data, check=check)
    fixture.write_bytes(compressed_elf(stream))
    assert run(fixture, 'compressed') == {'_Zlocal': '_Zlocal.__uniq.123', '_Zlto': '_Zlto.__uniq.77.llvm.88', '_Zglobal': '_Zglobal.__uniq.4'}
    compressed_cases += 1
    for damaged in (stream[:-1], stream[:10], b'not-xz', stream + b'trailing'):
        fixture.write_bytes(compressed_elf(damaged))
        assert run(fixture, "reject") == {}
        compressed_cases += 1
# Bounded expansion: a valid stream exceeding the 32 MiB decoded limit rejects.
fixture.write_bytes(compressed_elf(lzma.compress(data + b'\0' * (33 * 1024 * 1024))))
assert run(fixture, "reject") == {}
compressed_cases += 1
# Malformed decompressed ELF is rejected after a successful integrity check.
fixture.write_bytes(compressed_elf(lzma.compress(data[:63])))
assert run(fixture, "reject") == {}
compressed_cases += 1

# Two native symbol tables disagree on one canonical stem. Neither address
# participates: the other unique global name remains independently resolvable.
extra_strings = b'\0_Zlocal.__uniq.999\0'
extra_records = b'\0' * 24 + struct.pack('<IBBHQQ', 1, 2, 0, 1, 0x12345678, 0)
multi_header = struct.pack('<16sHHIQQQIHHHHHH', b'\x7fELF\x02\x01\x01' + b'\0' * 9,
                           3, 183, 1, 0, 0, 64, 0, 64, 0, 0, 64, 5, 0)
start = 384
multi_sections = b'\0' * 64
multi_sections += struct.pack('<IIQQQQIIQQ', 0, 2, 0, 0, start, len(records), 2, 0, 8, 24)
multi_sections += struct.pack('<IIQQQQIIQQ', 0, 3, 0, 0, start + len(records), len(strings), 0, 0, 1, 0)
start += len(records) + len(strings)
multi_sections += struct.pack('<IIQQQQIIQQ', 0, 2, 0, 0, start, len(extra_records), 4, 0, 8, 24)
multi_sections += struct.pack('<IIQQQQIIQQ', 0, 3, 0, 0, start + len(extra_records), len(extra_strings), 0, 0, 1, 0)
fixture.write_bytes(compressed_elf(lzma.compress(multi_header + multi_sections + records + strings + extra_records + extra_strings)))
assert run(fixture) == {'_Zglobal': '_Zglobal.__uniq.4', '_Zlto': '_Zlto.__uniq.77.llvm.88'}
compressed_cases += 1

# Actual Android 14 LTO CheckJNI names: text-only inventory, no runtime bytes.
observed = re.findall(r'"([^"\n]+)"', (root / 'tests/host/fixtures/android14_checkjni_names.h').read_text())
assert len(observed) == 36
check_strings = b'\0'
check_records = b'\0' * 24
for name in observed:
    check_records += struct.pack('<IBBHQQ', len(check_strings), 18, 0, 1, 0xdeadbeef, 0)
    check_strings += name.encode() + b'\0'
check_sections = b'\0' * 64
check_sections += struct.pack('<IIQQQQIIQQ', 0, 2, 0, 0, 256, len(check_records), 2, 0, 8, 24)
check_sections += struct.pack('<IIQQQQIIQQ', 0, 3, 0, 0, 256 + len(check_records), len(check_strings), 0, 0, 1, 0)
fixture.write_bytes(compressed_elf(lzma.compress(header + check_sections + check_records + check_strings)))
assert len(run(fixture, 'checkjni')) == 36
compressed_cases += 1

devices = []
for manifest in args.manifests:
    device = json.loads(manifest.read_text())
    assert device['complete']
    library = next(lib for lib in device['libraries'] if lib['name'] == 'libart.so')
    file = manifest.parent / library['file']
    assert hashlib.sha256(file.read_bytes()).hexdigest() == library['sha256']
    names = run(file)
    raw_inventory = (file.parent / 'defined-symbols.json').read_bytes()
    assert hashlib.sha256(raw_inventory).hexdigest() == library['symbolInventorySha256']
    inventory = set(json.loads(raw_inventory))
    mini_names, mini = mini_symbol_inventory(file.read_bytes())
    if "miniDebugData" in library:
        assert library["miniDebugData"] == mini
    inventory.update(mini_names)
    assert all(full in inventory and full.startswith(base + '.__uniq.') for base, full in names.items())
    canonical = '_ZN3artL12Field_getIntEP7_JNIEnvP8_jobjectS3_'
    candidates = [name for name in inventory if name.startswith(canonical + '.__uniq.')]
    if len(candidates) == 1:
        assert names[canonical] == candidates[0]
    devices.append(dict(manifest=str(manifest), uniqueNames=len(names), fieldGetInt=names.get(canonical)))
report = dict(syntheticNegativeCases=negative_count, compressedCases=compressed_cases, devices=devices,
              result='Passed address/undefined sanitizers; unique names only, ambiguous names omitted')
# Exercise actual HookEngine name fallback on fixed ELF64.
arm64_fixture = out / "arm64-names.elf"
arm64_fixture.write_bytes(data)
integration = out / 'shadow-test'
subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-O1', '-g',
                '-DXZ_USE_CRC64', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-I', str(cpp), '-I', str(root / 'tests/host/shadowhook_stub'),
                str(root / 'tests/host/shadow_unique_symbol_test.cc'),
                str(cpp / 'hooks/shadow_hook_engine.cc'),
                str(cpp / 'support/elf_symbol_names.cc'), *xz_objects, '-o', str(integration)], check=True)
synthetic = subprocess.run([str(integration), str(arm64_fixture), '_Zlocal', '_Zlocal.__uniq.123'],
                           capture_output=True, text=True,
                           env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
synthetic.check_returncode()
report['shadowArm64Integration'] = synthetic.stdout.strip()
if args.shadow_fixture:
    result = subprocess.run([str(integration), str(args.shadow_fixture)], capture_output=True, text=True,
                            env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
    result.check_returncode()
    report['shadowIntegration'] = result.stdout.strip()
(out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
