#!/usr/bin/env python3
"""Compile each fixture class into its own ART JVMTI single-class dex."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import struct
import hashlib
import zlib

p = argparse.ArgumentParser()
p.add_argument('--sdk', required=True)
p.add_argument('--java', required=True)
p.add_argument('--out', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
out = Path(a.out)
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, JAVA_HOME=a.java)
env['PATH'] = str(Path(a.java) / 'bin') + os.pathsep + env['PATH']
for variant in sorted((root / 'fixtures').iterdir()):
    sources = sorted(variant.rglob('*.java'))
    if not sources:
        continue
    with tempfile.TemporaryDirectory(prefix='art-dcevm-fixtures-') as work:
        classes = Path(work) / 'classes'
        classes.mkdir()
        subprocess.run([str(Path(a.java)/'bin/javac'), '--release', '8', '-encoding', 'UTF-8', '-g', '-d', str(classes), *map(str, sources)], check=True, env=env)
        for klass in sorted(classes.rglob('*.class')):
            dexdir = Path(work) / ('dex-' + klass.stem)
            dexdir.mkdir()
            subprocess.run([str(Path(a.sdk)/'build-tools/35.0.0/d8'), '--min-api', '33', '--lib',
                            str(Path(a.sdk)/'platforms/android-35/android.jar'),
                            '--output', str(dexdir), str(klass)], check=True, env=env)
            dest = out / variant.name / klass.relative_to(classes).with_suffix('.dex')
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes((dexdir/'classes.dex').read_bytes())
            print(dest.relative_to(out))

# Keep the DEX structure, signature and checksum valid, but make compute()I
# return an object register. ART's instruction/type verifier must reject this
# after allocating a replacement with a new superclass, before any commit.
source = out / 'hier-parent/dev/artdcevm/fixture/HierarchySubject.dex'
if source.exists():
    data = bytearray(source.read_bytes())
    def u32(offset):
        return struct.unpack_from('<I', data, offset)[0]
    def uleb(offset):
        result = shift = 0
        while True:
            byte = data[offset]; offset += 1
            result |= (byte & 127) << shift
            if not byte & 128:
                return result, offset
            shift += 7
    def string(index):
        offset = u32(u32(60) + 4 * index)
        _, offset = uleb(offset)
        return bytes(data[offset:data.index(0, offset)]).decode('utf-8')
    position = u32(u32(100) + 24)
    counts = []
    for _ in range(4):
        count, position = uleb(position); counts.append(count)
    for _ in range(counts[0] + counts[1]):
        _, position = uleb(position); _, position = uleb(position)
    changed = False
    for count in counts[2:]:
        index = 0
        for _ in range(count):
            difference, position = uleb(position); index += difference
            _, position = uleb(position); code, position = uleb(position)
            if string(u32(u32(92) + index * 8 + 4)) == 'compute':
                instruction = code + 16 + u32(code + 12) * 2 - 2
                assert code and data[instruction] == 0x0f, 'Expected int return in hierarchy fixture'
                data[instruction] = 0x11
                changed = True
    assert changed, 'compute() not found in hierarchy fixture'
    data[12:32] = hashlib.sha1(data[32:]).digest()
    struct.pack_into('<I', data, 8, zlib.adler32(data[12:]) & 0xffffffff)
    dest = out / 'hier-invalid/dev/artdcevm/fixture/HierarchySubject.dex'
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(data)
    print(dest.relative_to(out))
