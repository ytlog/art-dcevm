#!/usr/bin/env python3
"""Verify the production D8 fixture preserves a missing field declaration."""
import argparse, hashlib, struct, subprocess, zlib
from pathlib import Path
root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--sdk', type=Path, default=Path.home()/'Library/Android/sdk')
p.add_argument('--java', type=Path, default=Path('/Applications/Android Studio.app/Contents/jbr/Contents/Home'))
a = p.parse_args()
out = root/'build/evidence/field-factory-observer'
out.mkdir(parents=True, exist_ok=True)
assets = out/'assets'
with (out/'build.log').open('w') as log:
    subprocess.run(['python3', str(root/'tools/build-production-bootstrap.py'), '--sdk', str(a.sdk), '--java', str(a.java), '--out', str(assets)], stdout=log, stderr=log, check=True)
data = (assets/'artdcevm/bootstrap/missing-optional.dex').read_bytes()
def verify(data):
    def u32(p): return struct.unpack_from('<I', data, p)[0]
    assert data[:4] == b'dex\n' and data[4:8] in (b'035\0', b'037\0', b'038\0', b'039\0')
    assert u32(32) == len(data)
    assert data[12:32] == hashlib.sha1(data[32:]).digest()
    assert u32(8) == zlib.adler32(data[12:]) & 0xffffffff
    strings = []
    for i in range(u32(56)):
        off = u32(u32(60)+i*4)
        while data[off] & 128: off += 1
        off += 1
        strings.append(data[off:data.index(0, off)].decode('utf-8'))
    types = [strings[u32(u32(68)+i*4)] for i in range(u32(64))]
    classes = [types[u32(u32(100)+i*32)] for i in range(u32(96))]
    holder = 'Lcom/github/ytlog/artdcevm/internal/MissingOptionalFields;'
    missing = 'Lcom/github/ytlog/artdcevm/internal/MissingOptionalType;'
    assert classes == [holder], 'Compile-only optional class must never be a runtime class definition'
    fields = set()
    for i in range(u32(80)):
        owner, typ, name = struct.unpack_from('<HHI', data, u32(84)+i*8)
        fields.add((types[owner], strings[name], types[typ]))
    assert fields == {(holder, 'number', 'I'), (holder, 'optional', missing)}
verify(data)
# Independent negative: putting the missing type in class_def must be refused,
# even after recreating a valid DEX signature/checksum.
bad = bytearray(data)
type_count, type_offset = struct.unpack_from('<II', data, 64)
string_offset = struct.unpack_from('<I', data, 60)[0]
for i in range(type_count):
    index = struct.unpack_from('<I', data, type_offset+i*4)[0]
    off = struct.unpack_from('<I', data, string_offset+index*4)[0]
    while data[off] & 128: off += 1
    off += 1
    if data[off:data.index(0,off)].endswith(b'/MissingOptionalType;'): break
else: raise AssertionError('Missing type descriptor absent')
struct.pack_into('<I', bad, struct.unpack_from('<I',data,100)[0], i)
bad[12:32] = hashlib.sha1(bad[32:]).digest()
struct.pack_into('<I',bad,8,zlib.adler32(bad[12:])&0xffffffff)
try: verify(bytes(bad))
except AssertionError: pass
else: raise AssertionError('Included missing class incorrectly accepted')
classes = out/'java';classes.mkdir(exist_ok=True)
sources = [root/f'artdcevm/src/main/java/com/github/ytlog/artdcevm/internal/{n}.java' for n in ('ProductionBootstrap','EvolutionCalibration','CalibrationFixture')]
with (out/'java.log').open('w') as log:
    subprocess.run([str(a.java/'bin/javac'),'-cp',str(a.sdk/'platforms/android-35/android.jar'),'-d',str(classes),*map(str,sources)],stdout=log,stderr=log,check=True)
result = 'PASS: actual D8 holder-only class_def, exact primitive/absent-reference field IDs; included-class negative rejected; production Java compiles. ART force=false behavior requires device observation.'
(out/'result.log').write_text(result+'\n')
print(result)
