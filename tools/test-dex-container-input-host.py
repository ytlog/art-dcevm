#!/usr/bin/env python3
"""Verify replacement metadata parsing for single-file DEX 040/041."""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import zlib
root = Path(__file__).resolve().parents[1]
out = root / 'build/evidence/host/dex-container-input'
out.mkdir(parents=True, exist_ok=True)
def fixture(version):
    header = 0x78 if version == '041' else 0x70
    data = bytearray(header)
    def put(offset, value):
        struct.pack_into('<I', data, offset, value)
    tables = {}
    for field, count, width in [(56, 6, 4), (64, 4, 4), (72, 1, 12),
                                (80, 1, 8), (88, 1, 8), (96, 1, 32)]:
        tables[field] = len(data)
        put(field, count); put(field + 4, len(data))
        data.extend(bytes(count * width))
    data_start = len(data)
    def leb(value):
        result = bytearray()
        while value >= 128:
            result.append((value & 127) | 128); value >>= 7
        return result + bytes([value])
    for i, value in enumerate(['I', 'Ljava/lang/Object;', 'Ltest/ContainerSubject;', 'V', 'run', 'value']):
        put(tables[56] + i * 4, len(data))
        data.extend(leb(len(value)) + value.encode() + b'\0')
    for i in range(4): put(tables[64] + i * 4, i)
    struct.pack_into('<III', data, tables[72], 3, 3, 0)
    struct.pack_into('<HHI', data, tables[80], 2, 0, 5)
    struct.pack_into('<HHI', data, tables[88], 2, 0, 4)
    data.extend(bytes((-len(data)) % 4))
    code = len(data)
    data.extend(struct.pack('<HHHHIIH', 0, 0, 0, 0, 0, 1, 0x000e))
    class_data = len(data)
    data.extend(bytes([0, 1, 1, 0, 0, 1, 0, 9]) + leb(code))
    struct.pack_into('<IIIIIIII', data, tables[96], 2, 1, 1, 0, 0xffffffff, 0, class_data, 0)
    data.extend(bytes((-len(data)) % 4))
    map_offset = len(data)
    entries = [(0, 1, 0), (1, 6, tables[56]), (2, 4, tables[64]), (3, 1, tables[72]),
               (4, 1, tables[80]), (5, 1, tables[88]), (6, 1, tables[96]),
               (0x2002, 6, data_start), (0x2001, 1, code), (0x2000, 1, class_data),
               (0x1000, 1, map_offset)]
    data.extend(struct.pack('<I', len(entries)))
    for kind, count, offset in entries: data.extend(struct.pack('<HHII', kind, 0, count, offset))
    data[:8] = b'dex\n' + version.encode() + b'\0'
    put(32, len(data)); put(36, header); put(40, 0x12345678); put(52, map_offset)
    if version == '041': put(0x70, len(data)); put(0x74, 0)
    else: put(104, len(data) - data_start); put(108, data_start)
    data[12:32] = hashlib.sha1(data[32:]).digest()
    put(8, zlib.adler32(data[12:]))
    path = out / ('subject-' + version + '.dex'); path.write_bytes(data)
    return path
cpp = root / 'artdcevm/src/main/cpp'
binary = out / 'dex_container_input_test'
subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(cpp),
                str(root / 'tests/host/dex_container_input_test.cc'), str(cpp / 'evolution/dex_schema.cc'),
                '-o', str(binary)], check=True)
result = subprocess.run([str(binary), str(fixture('040')), str(fixture('041'))], capture_output=True, text=True,
                        env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
(out / 'result.txt').write_text(result.stdout + result.stderr)
result.check_returncode()
print(result.stdout.strip())
print('DEX container input: passed (ASan/UBSan)')
