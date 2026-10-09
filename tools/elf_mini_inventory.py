"""Bounded, read-only .gnu_debugdata name inventory; never resolve addresses."""
import hashlib
import json
import lzma
import struct

LIMIT = 32 * 1024 * 1024


def _range(data, at, size):
    if at > len(data) or size > len(data) - at:
        raise ValueError('ELF range exceeds source bytes')
    return data[at:at + size]


def _sections(data):
    identity = _range(data, 0, 7)
    if identity[:4] != b'\x7fELF' or identity[5:7] != b'\x01\x01' or identity[4] not in (1, 2):
        raise ValueError('Expected little-endian ARM ELF32/64')
    wide = identity[4] == 2
    header_size, section_size, machine = (64, 64, 183) if wide else (52, 40, 40)
    values = struct.unpack('<16sHHIQQQIHHHHHH' if wide else '<16sHHIIIIIHHHHHH', _range(data, 0, header_size))
    if values[1:4] != (3, machine, 1) or values[11] != section_size or not values[12]:
        raise ValueError('Expected ARM ET_DYN section metadata matching ELF width')
    table = _range(data, values[6], values[12] * section_size)
    sections = [struct.unpack_from('<IIQQQQIIQQ' if wide else '<IIIIIIIIII', table, at)
                for at in range(0, len(table), section_size)]
    return sections, values[13]


def _string(table, offset):
    if offset >= len(table):
        raise ValueError('ELF string offset exceeds table')
    end = table.find(b'\0', offset)
    if end < 0:
        raise ValueError('Unterminated ELF string')
    return table[offset:end].decode('utf-8')


def _defined_names(data):
    sections, _ = _sections(data)
    expected_stride = 24 if data[4] == 2 else 16
    names = set()
    for section in sections:
        _, kind, _, _, at, size, link, _, _, stride = section
        if kind not in (2, 11):
            continue
        if stride != expected_stride or size % stride or link >= len(sections):
            raise ValueError('Invalid mini symbol table')
        strings = sections[link]
        if strings[1] != 3:
            raise ValueError('Mini symbols do not reference a string table')
        table = _range(data, strings[4], strings[5])
        symbols = _range(data, at, size)
        for offset in range(0, size, stride):
            # Read name/binding/section only. st_value and st_size are not used.
            name = struct.unpack_from('<I', symbols, offset)[0]
            index = struct.unpack_from('<H', symbols, offset + (6 if data[4] == 2 else 14))[0]
            if index == 0:
                continue
            names.add(_string(table, name))
    names.discard('')
    return sorted(names)


def mini_symbol_inventory(data):
    """Return names + content provenance; name presence proves no callability."""
    sections, string_index = _sections(data)
    if not string_index:
        return [], {'present': False}
    if string_index >= len(sections) or sections[string_index][1] != 3:
        raise ValueError('Invalid ELF section-name table')
    strings = sections[string_index]
    table = _range(data, strings[4], strings[5])
    found = [section for section in sections if _string(table, section[0]) == '.gnu_debugdata']
    if not found:
        return [], {'present': False}
    if len(found) != 1 or found[0][1] != 1 or found[0][5] > LIMIT:
        raise ValueError('Invalid or oversized .gnu_debugdata')
    compressed = _range(data, found[0][4], found[0][5])
    decoder = lzma.LZMADecompressor(format=lzma.FORMAT_XZ, memlimit=64 * 1024 * 1024)
    try:
        decoded = decoder.decompress(compressed, max_length=LIMIT + 1)
    except lzma.LZMAError as error:
        raise ValueError('Invalid .gnu_debugdata XZ stream') from error
    if len(decoded) > LIMIT or not decoder.eof or decoder.unused_data:
        raise ValueError('Truncated, trailing or oversized mini ELF stream')
    if decoder.check not in (lzma.CHECK_NONE, lzma.CHECK_CRC32, lzma.CHECK_CRC64):
        raise ValueError('Unsupported mini ELF integrity check')
    if len(decoded) < 5 or decoded[4] != data[4]:
        raise ValueError('Mini ELF width differs from outer ELF')
    names = _defined_names(decoded)
    inventory = (json.dumps(names, indent=2) + '\n').encode()
    return names, {'present': True, 'format': 'XZ ELF' + ('64' if data[4] == 2 else '32') + ' named symbols',
                   'compressedSha256': hashlib.sha256(compressed).hexdigest(),
                   'decodedSha256': hashlib.sha256(decoded).hexdigest(),
                   'nameInventorySha256': hashlib.sha256(inventory).hexdigest(),
                   'compressedBytes': len(compressed), 'decodedBytes': len(decoded),
                   'definedSymbolCount': len(names),
                   'scope': 'Names only; no address, ABI, loader or callability proof'}
