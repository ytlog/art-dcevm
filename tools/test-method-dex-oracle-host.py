#!/usr/bin/env python3
"""Exercise APK DEX identity parsing against D8 output and malformed/041 containers."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import zipfile
import zlib

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--d8', type=Path, default=Path.home() / 'Library/Android/sdk/build-tools/36.0.0/d8')
args = parser.parse_args()
out = root / 'build/evidence/host/method-dex-oracle'
classes = out / 'classes'
dexdir = out / 'dex'
classes.mkdir(parents=True, exist_ok=True)
dexdir.mkdir(exist_ok=True)
java = root / 'app/src/debug/java/com/github/ytlog/artdcevm/testing'
subprocess.run(['javac', '-d', str(classes), str(java / 'MethodIdentityFixture.java'),
                str(java / 'MethodDexOracle.java'), str(root / 'tests/host/java/MethodDexOracleTest.java')], check=True)
fixture = classes / 'com/github/ytlog/artdcevm/testing/MethodIdentityFixture.class'
subprocess.run([str(args.d8), '--min-api', '30', '--output', str(dexdir), str(fixture)], check=True)
real = (dexdir / 'classes.dex').read_bytes()

def uleb(value):
    result = bytearray()
    while value >= 128:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return result

def checksum(data):
    data = bytearray(data)
    data[12:32] = hashlib.sha1(data[32:]).digest()
    struct.pack_into('<I', data, 8, zlib.adler32(data[12:]))
    return data

def container_fixture(base=0):
    owner = 'Lcom/github/ytlog/artdcevm/testing/MethodIdentityFixture;'
    strings = sorted({owner, 'I', 'J', 'V', 'II', 'JJ', 'instanceMarker', 'nativeMarker', 'staticMarker'})
    types = sorted(['I', 'J', 'V', owner], key=strings.index)
    data = bytearray(120)
    def append(value, alignment=1):
        while (base + len(data)) % alignment:
            data.append(0)
        offset = base + len(data)
        data.extend(value)
        return offset
    stringoff = append(bytes(len(strings) * 4), 4)
    typeoff = append(bytes(len(types) * 4), 4)
    protooff = append(bytes(3 * 12), 4)
    methodoff = append(bytes(3 * 8), 4)
    classoff = append(bytes(32), 4)
    for i, value in enumerate(strings):
        offset = append(uleb(len(value)) + value.encode() + b'\0')
        struct.pack_into('<I', data, stringoff - base + i * 4, offset)
    for i, value in enumerate(types):
        struct.pack_into('<I', data, typeoff - base + i * 4, strings.index(value))
    for i, (result, parameter, shorty) in enumerate([('I', 'I', 'II'), ('J', 'J', 'JJ'), ('V', None, 'V')]):
        parameters = append(struct.pack('<IH', 1, types.index(parameter)), 4) if parameter else 0
        struct.pack_into('<III', data, protooff - base + i * 12,
                         strings.index(shorty), types.index(result), parameters)
    for i, (name, proto) in enumerate([('instanceMarker', 0), ('nativeMarker', 2), ('staticMarker', 1)]):
        struct.pack_into('<HHI', data, methodoff - base + i * 8,
                         types.index(owner), proto, strings.index(name))
    instancecode = append(struct.pack('<HHHHIIH', 2, 2, 0, 0, 0, 1, 0x010f), 4)
    staticcode = append(struct.pack('<HHHHIIHHH', 2, 2, 0, 0, 0, 3, 0x0016, 0, 0x0010), 4)
    classdata = append(uleb(0) + uleb(0) + uleb(2) + uleb(1) +
                       uleb(1) + uleb(0x102) + uleb(0) +
                       uleb(1) + uleb(0x19) + uleb(staticcode) +
                       uleb(0) + uleb(1) + uleb(instancecode))
    struct.pack_into('<IIIIIIII', data, classoff - base, types.index(owner), 0x11, 0xffffffff,
                     0, 0xffffffff, 0, classdata, 0)
    data[:8] = b'dex\n041\0'
    for offset, value in [(32, len(data)), (36, 120), (40, 0x12345678),
                          (56, len(strings)), (60, stringoff), (64, len(types)), (68, typeoff),
                          (72, 3), (76, protooff), (88, 3), (92, methodoff), (96, 1), (100, classoff),
                          (112, base + len(data)), (116, base)]:
        struct.pack_into('<I', data, offset, value)
    return checksum(data)

apks = []
def apk(name, dex, duplicate=False):
    path = out / (name + '.apk')
    with zipfile.ZipFile(path, 'w') as archive:
        archive.writestr('classes.dex', dex)
        if duplicate:
            archive.writestr('classes2.dex', dex)
    apks.append(path)

for version in ['035', '037', '038', '039', '040']:
    data = bytearray(real)
    data[4:7] = version.encode()
    apk('legacy-' + version, checksum(data))
one = container_fixture()
apk('container-single', one)
second = container_fixture(120)
first = bytearray(120)
first[:8] = b'dex\n041\0'
for offset, value in [(32, 120), (36, 120), (40, 0x12345678), (112, 120 + len(second)), (116, 0)]:
    struct.pack_into('<I', first, offset, value)
apk('container-multi', checksum(first) + second)
apk('reject-duplicate-owner', real, duplicate=True)
apk('reject-truncated', real[:112])
data = bytearray(real); data[4:7] = b'036'; apk('reject-version', data)
data = bytearray(real); struct.pack_into('<I', data, 60, len(data) - 2); apk('reject-table-bounds', data)
data = bytearray(real); struct.pack_into('<I', data, 100, 0); apk('reject-class-offset', data)
data = bytearray(real)
protooff = struct.unpack_from('<I', data, 76)[0]
struct.pack_into('<I', data, protooff + 4, 0x7fffffff); apk('reject-prototype-type', data)
data = bytearray(one); struct.pack_into('<I', data, 116, 4); apk('reject-container-header-offset', data)
data = bytearray(one); struct.pack_into('<I', data, 112, len(data) + 1); apk('reject-container-size', data)
result = subprocess.run(['java', '-cp', str(classes), 'MethodDexOracleTest', *map(str, apks)],
                        capture_output=True, text=True)
(out / 'result.log').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
