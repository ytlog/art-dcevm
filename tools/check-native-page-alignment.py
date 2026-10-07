#!/usr/bin/env python3
"""Check every packaged native ELF and uncompressed APK entry for 16 KB alignment."""
import argparse
import json
from pathlib import Path
import struct
import zipfile


def check_elf(data, page_size):
    if data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Expected a 64-bit little-endian ELF')
    phoff = struct.unpack_from('<Q', data, 32)[0]
    phentsize, phnum = struct.unpack_from('<HH', data, 54)
    if phentsize < 56:
        raise ValueError('Invalid ELF program header size')
    loads = []
    for i in range(phnum):
        kind, flags, offset, vaddr, _, file_size, mem_size, align = struct.unpack_from(
            '<IIQQQQQQ', data, phoff + i * phentsize)
        if kind != 1:  # PT_LOAD, per the ELF specification.
            continue
        passed = (align >= page_size and align & (align - 1) == 0 and
                  (offset - vaddr) % page_size == 0 and file_size <= mem_size and
                  offset + file_size <= len(data))
        loads.append({'offset': offset, 'virtualAddress': vaddr, 'alignment': align, 'passed': passed})
    return {'segments': loads, 'passed': bool(loads) and all(x['passed'] for x in loads)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifact', type=Path, help='APK or AAR to check')
    parser.add_argument('--page-size', type=int, choices=(4096, 16384), default=16384)
    args = parser.parse_args()
    result = {'artifact': str(args.artifact.resolve()), 'pageSize': args.page_size, 'libraries': []}
    try:
        with zipfile.ZipFile(args.artifact) as archive, args.artifact.open('rb') as raw:
            for entry in archive.infolist():
                if not entry.filename.endswith('.so'):
                    continue
                item = {'path': entry.filename, **check_elf(archive.read(entry), args.page_size)}
                if args.artifact.suffix == '.apk' and entry.compress_type == zipfile.ZIP_STORED:
                    raw.seek(entry.header_offset)
                    header = raw.read(30)
                    name_size, extra_size = struct.unpack_from('<HH', header, 26)
                    item['zipDataOffset'] = entry.header_offset + 30 + name_size + extra_size
                    item['passed'] &= item['zipDataOffset'] % args.page_size == 0
                result['libraries'].append(item)
        result['passed'] = bool(result['libraries']) and all(x['passed'] for x in result['libraries'])
    except (OSError, ValueError, struct.error, zipfile.BadZipFile) as error:
        result.update(passed=False, error=str(error))
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
