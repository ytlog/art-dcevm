#!/usr/bin/env python3
"""Check bounded collection/audit mini-name inventory against real ELF sources."""
import hashlib
import json
import lzma
from pathlib import Path
import struct
import sys
from elf_mini_inventory import mini_symbol_inventory


def wrap(stream, width=2):
    names = b'\0.shstrtab\0.gnu_debugdata\0'
    hs, ss, machine = (64, 64, 183) if width == 2 else (52, 40, 40)
    header = struct.pack('<16sHHIQQQIHHHHHH' if width == 2 else '<16sHHIIIIIHHHHHH',
                         b'\x7fELF' + bytes([width, 1, 1]) + b'\0' * 9,
                         3, machine, 1, 0, 0, hs, 0, hs, 0, 0, ss, 3, 1)
    start = hs + 3 * ss
    fmt = '<IIQQQQIIQQ' if width == 2 else '<IIIIIIIIII'
    sections = b'\0' * ss
    sections += struct.pack(fmt, 1, 3, 0, 0, start, len(names), 0, 0, 1, 0)
    sections += struct.pack(fmt, 11, 1, 0, 0, start + len(names), len(stream), 0, 0, 1, 0)
    return header + sections + names + stream


def reject(data):
    try:
        mini_symbol_inventory(data)
    except ValueError:
        return
    raise AssertionError('Invalid mini source accepted')


# Symbols have arbitrary values; only the name + defined section are relevant.
strings = b'\0_Zobserved.__uniq.123\0_Zundefined\0'
records = b'\0' * 24 + struct.pack('<IBBHQQ', 1, 18, 0, 1, 0xdeadbeef, 0)
records += struct.pack('<IBBHQQ', 22, 18, 0, 0, 0, 0)
header = struct.pack('<16sHHIQQQIHHHHHH', b'\x7fELF\x02\x01\x01' + b'\0' * 9,
                     3, 183, 1, 0, 0, 64, 0, 64, 0, 0, 64, 3, 0)
sections = b'\0' * 64
sections += struct.pack('<IIQQQQIIQQ', 0, 2, 0, 0, 256, len(records), 2, 0, 8, 24)
sections += struct.pack('<IIQQQQIIQQ', 0, 3, 0, 0, 256 + len(records), len(strings), 0, 0, 1, 0)
mini = header + sections + records + strings
assert mini_symbol_inventory(mini) == ([], {'present': False})
for check in (lzma.CHECK_NONE, lzma.CHECK_CRC32, lzma.CHECK_CRC64):
    stream = lzma.compress(mini, check=check)
    names, provenance = mini_symbol_inventory(wrap(stream))
    assert names == ['_Zobserved.__uniq.123']
    assert provenance['decodedSha256'] == hashlib.sha256(mini).hexdigest()
    assert provenance['compressedSha256'] == hashlib.sha256(stream).hexdigest()
    for invalid in (stream[:-1], stream + b'trailing', b'not-xz'):
        reject(wrap(invalid))
reject(wrap(lzma.compress(mini, check=lzma.CHECK_SHA256)))
reject(wrap(lzma.compress(b'invalid-elf')))
reject(wrap(lzma.compress(mini + b'\0' * (33 * 1024 * 1024))))
for size in (0, 10, 63, 100):
    reject(wrap(lzma.compress(mini))[:size])
# ELF32 symbols place section identity after value/size; those arbitrary
# numbers must never become names or resolved addresses.
records32 = b'\0' * 16 + struct.pack('<IIIBBH', 1, 0xdeadbeef, 0, 18, 0, 1)
records32 += struct.pack('<IIIBBH', 22, 0, 0, 18, 0, 0)
header32 = struct.pack('<16sHHIIIIIHHHHHH', b'\x7fELF\x01\x01\x01' + b'\0' * 9,
                       3, 40, 1, 0, 0, 52, 0, 52, 0, 0, 40, 3, 0)
sections32 = b'\0' * 40
sections32 += struct.pack('<IIIIIIIIII', 0, 2, 0, 0, 172, len(records32), 2, 0, 4, 16)
sections32 += struct.pack('<IIIIIIIIII', 0, 3, 0, 0, 172 + len(records32), len(strings), 0, 0, 1, 0)
mini32 = header32 + sections32 + records32 + strings
for check in (lzma.CHECK_NONE, lzma.CHECK_CRC32, lzma.CHECK_CRC64):
    stream = lzma.compress(mini32, check=check)
    names, provenance = mini_symbol_inventory(wrap(stream, 1))
    assert names == ['_Zobserved.__uniq.123']
    assert provenance['format'] == 'XZ ELF32 named symbols'
    for invalid in (stream[:-1], stream + b'trailing'):
        reject(wrap(invalid, 1))
reject(wrap(lzma.compress(mini32)))
reject(wrap(lzma.compress(mini), 1))
wrong_machine = bytearray(mini32)
struct.pack_into('<H', wrong_machine, 18, 183)
reject(wrap(lzma.compress(wrong_machine), 1))
real = []
for arg in sys.argv[1:]:
    path = Path(arg)
    names, provenance = mini_symbol_inventory(path.read_bytes())
    assert provenance['present'] and names
    # Actual fixed named source entries, not assumed addresses/callability.
    assert any(name.startswith('_ZN3art12_GLOBAL__N_120MethodHandleFieldGet') for name in names)
    real.append({'file': str(path), **provenance})
print(json.dumps({'passed': True, 'realSources': real, 'scope': 'read-only names/content provenance'}, indent=2))
