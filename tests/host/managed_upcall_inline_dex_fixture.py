"""Valid independent StandardDEX 040 namespaces for managed inline upcalls.

make(unsafe=False) returns (bytes, parsed Method inventory).
make_dispatch()/make_receiver() return (bytes, parsed Namespace).
write(out) writes all four DEX files and returns inventories/namespace metadata.
"""
from dataclasses import dataclass
from pathlib import Path
import hashlib
import struct
import zlib

OWNER = 'Lcom/example/BootInline;'
NATIVE = 'Lcom/example/Native;'
DISPATCH = 'Lcom/example/Dispatch;'
RECEIVER = 'Lcom/example/Receiver;'
OBJECT = 'Ljava/lang/Object;'

@dataclass(frozen=True)
class Method:
    name: str
    index: int
    code_offset: int
    instructions: bytes
    tries_count: int
    handler_offset: int
    handler_pc: int | None
    owner: str
    descriptor: str
    access: int
    registers: int
    ins: int
    outs: int
    direct: bool

@dataclass(frozen=True)
class Namespace:
    descriptor: str
    access: int
    superclass: str
    interfaces: tuple[str, ...]
    methods: dict[str, Method]
    references: tuple[tuple[str, str, str], ...]


def leb(value):
    assert value >= 0
    out = bytearray()
    while value >= 128:
        out.append((value & 127) | 128)
        value >>= 7
    return out + bytes([value])


def parse_namespace(data):
    """Re-read complete declarations, prototypes, ClassData and CodeItems."""
    def u32(at):
        return struct.unpack_from('<I', data, at)[0]
    def read_leb(at):
        value = 0
        for shift in range(0, 35, 7):
            byte = data[at]
            at += 1
            value |= (byte & 127) << shift
            if byte < 128:
                return value, at
        raise ValueError('invalid ULEB128')
    assert data[:8] == b'dex\n040\0' and u32(32) == len(data)
    assert hashlib.sha1(data[32:]).digest() == data[12:32]
    assert zlib.adler32(data[12:]) == u32(8)
    strings = []
    for i in range(u32(56)):
        at = u32(u32(60) + i * 4)
        length, at = read_leb(at)
        text = data[at:data.index(0, at)].decode()
        assert len(text) == length
        strings.append(text)
    types = [strings[u32(u32(68) + i * 4)] for i in range(u32(64))]
    def type_list(at):
        if not at:
            return ()
        return tuple(types[struct.unpack_from('<H', data, at + 4 + i * 2)[0]]
                     for i in range(u32(at)))
    protos = []
    for i in range(u32(72)):
        shorty, ret, params = struct.unpack_from('<III', data, u32(76) + i * 12)
        parameters = type_list(params)
        assert strings[shorty] == types[ret] + ''.join('L' for _ in parameters)
        protos.append('(' + ''.join(parameters) + ')' + types[ret])
    references = []
    for i in range(u32(88)):
        owner, proto, name = struct.unpack_from('<HHI', data, u32(92) + i * 8)
        references.append((types[owner], strings[name], protos[proto]))
    assert u32(96) == 1
    owner, access, superclass, interfaces, _, _, class_data, _ = struct.unpack_from('<IIIIIIII', data, u32(100))
    at = class_data
    counts = []
    for _ in range(4):
        n, at = read_leb(at)
        counts.append(n)
    assert counts[:2] == [0, 0]
    result = {}
    for direct, count in [(True, counts[2]), (False, counts[3])]:
        index = 0
        for _ in range(count):
            delta, at = read_leb(at)
            flags, at = read_leb(at)
            code, at = read_leb(at)
            index += delta
            method_owner, name, descriptor = references[index]
            assert method_owner == types[owner] and code % 4 == 0
            regs, ins, outs, tries, instructions = 0, 0, 0, 0, b''
            handler_off, handler_pc = 0, None
            if code:
                assert not flags & (0x100 | 0x400)
                regs, ins, outs, tries, debug, size = struct.unpack_from('<HHHHII', data, code)
                assert debug == 0
                instructions = data[code + 16:code + 16 + size * 2]
                assert len(instructions) == size * 2
                if tries:
                    assert tries == 1
                    try_at = code + 16 + size * 2 + (2 if size % 2 else 0)
                    start, length, handler_off = struct.unpack_from('<IHH', data, try_at)
                    assert (start, length, handler_off) == (0, 3, 1)
                    handlers = try_at + 8
                    count, cursor = read_leb(handlers)
                    assert count == 1 and cursor == handlers + handler_off
                    # SLEB128 size zero: catch-all without typed catches.
                    assert data[cursor] == 0
                    handler_pc, _ = read_leb(cursor + 1)
                    assert handler_pc == 4
            else:
                assert flags & (0x100 | 0x400)
            result[name] = Method(name, index, code, instructions, tries,
                                  handler_off, handler_pc, method_owner, descriptor,
                                  flags, regs, ins, outs, direct)
    return Namespace(types[owner], access, types[superclass], type_list(interfaces),
                     result, tuple(references))


def parse(data):
    return parse_namespace(data).methods


def _build(owner, class_access, interfaces, definitions, references, code_factory):
    """definitions: name -> (descriptor, access); code_factory uses real IDs."""
    refs = [(owner, name, desc) for name, (desc, _) in definitions.items()] + references
    parameters = {desc: (() if desc == '()V' else (DISPATCH,)) for _, _, desc in refs}
    types = sorted({owner, OBJECT, 'V', *interfaces, *(o for o, _, _ in refs),
                    *(t for params in parameters.values() for t in params)})
    strings = sorted({*types, *(name for _, name, _ in refs),
                      *('V' + 'L' * len(params) for params in parameters.values())})
    sid = {s: i for i, s in enumerate(strings)}
    tid = {s: i for i, s in enumerate(types)}
    protos = sorted(set(parameters.values()), key=lambda p: tuple(tid[t] for t in p))
    pid = {p: i for i, p in enumerate(protos)}
    methods = sorted(refs, key=lambda m: (tid[m[0]], sid[m[1]], pid[parameters[m[2]]]))
    mid = {name: i for i, (_, name, _) in enumerate(methods)}
    b = bytearray(112)
    tables = {}
    maps = [(0, 1, 0)]
    def put(at, v):
        struct.pack_into('<I', b, at, v)
    def align():
        b.extend(bytes((-len(b)) % 4))
    for header, count, width, tag in [(56, len(strings), 4, 1), (64, len(types), 4, 2),
                                    (72, len(protos), 12, 3), (88, len(methods), 8, 5), (96, 1, 32, 6)]:
        tables[header] = len(b)
        put(header, count)
        put(header + 4, len(b))
        maps.append((tag, count, len(b)))
        b.extend(bytes(count * width))
    start = len(b)
    maps.append((0x2002, len(strings), start))
    for i, s in enumerate(strings):
        put(tables[56] + i * 4, len(b))
        b.extend(leb(len(s)) + s.encode() + b'\0')
    for i, t in enumerate(types):
        put(tables[64] + i * 4, sid[t])
    lists = {}
    for params in sorted(set(protos + [tuple(interfaces)])):
        if not params:
            continue
        align()
        lists[params] = len(b)
        b.extend(struct.pack('<I', len(params)))
        b.extend(struct.pack('<' + 'H' * len(params), *(tid[t] for t in params)))
    if lists:
        maps.append((0x1001, len(lists), min(lists.values())))
    for i, params in enumerate(protos):
        struct.pack_into('<III', b, tables[72] + i * 12, sid['V' + 'L' * len(params)],
                         tid['V'], lists.get(params, 0))
    for i, (method_owner, name, desc) in enumerate(methods):
        struct.pack_into('<HHI', b, tables[88] + i * 8, tid[method_owner], pid[parameters[desc]], sid[name])
    codes = code_factory(mid)
    code_offsets = {}
    for method_owner, name, _ in methods:
        if method_owner != owner or name not in codes:
            continue
        regs, ins, outs, units, tries = codes[name]
        align()
        code_offsets[name] = len(b)
        b.extend(struct.pack('<HHHHII', regs, ins, outs, tries, 0, len(units)))
        b.extend(struct.pack('<' + 'H' * len(units), *units))
        if tries:
            if len(units) % 2:
                b.extend(b'\0\0')
            b.extend(struct.pack('<IHH', 0, 3, 1))
            b.extend(leb(1) + b'\0' + leb(4))
    if code_offsets:
        maps.append((0x2001, len(code_offsets), min(code_offsets.values())))
    direct, virtual = [], []
    for i, (method_owner, name, _) in enumerate(methods):
        if method_owner == owner:
            access = definitions[name][1]
            (direct if access & 8 else virtual).append((i, access, code_offsets.get(name, 0)))
    class_data = len(b)
    b.extend(leb(0) + leb(0) + leb(len(direct)) + leb(len(virtual)))
    for entries in (direct, virtual):
        previous = 0
        for i, access, code in entries:
            b.extend(leb(i - previous) + leb(access) + leb(code))
            previous = i
    maps.append((0x2000, 1, class_data))
    struct.pack_into('<IIIIIIII', b, tables[96], tid[owner], class_access, tid[OBJECT],
                     lists.get(tuple(interfaces), 0), 0xffffffff, 0, class_data, 0)
    align()
    mapoff = len(b)
    maps.append((0x1000, 1, mapoff))
    maps.sort(key=lambda item: item[2])
    b.extend(struct.pack('<I', len(maps)))
    for tag, count, offset in maps:
        b.extend(struct.pack('<HHII', tag, 0, count, offset))
    b[:8] = b'dex\n040\0'
    put(32, len(b))
    put(36, 112)
    put(40, 0x12345678)
    put(52, mapoff)
    put(104, len(b) - start)
    put(108, start)
    b[12:32] = hashlib.sha1(b[32:]).digest()
    put(8, zlib.adler32(b[12:]))
    data = bytes(b)
    return data, parse_namespace(data)


def make(unsafe=False):
    definitions = {name: ('()V', 9) for name in ('childEscape', 'inlineHandler', 'outerSafe')}
    definitions['interfaceCaller'] = ('(' + DISPATCH + ')V', 9)
    def codes(mid):
        def invoke(name):
            return [0x0071, mid[name], 0]
        return {
            'childEscape': (1, 0, 0, invoke('staticNative') + [0x000e], 0),
            'interfaceCaller': (1, 1, 1, [0x1072, mid['call'], 0, 0x000e], 0),
            'inlineHandler': (1, 0, 0, invoke('childEscape') + [0x000e, 0x000d]
                              + (invoke('staticNative') if unsafe else []) + [0x000e], 1),
            'outerSafe': (1, 0, 0, invoke('inlineHandler') + [0x000e, 0x000d, 0x000e], 1),
        }
    data, namespace = _build(OWNER, 1, (), definitions,
                             [(NATIVE, 'staticNative', '()V'), (DISPATCH, 'call', '()V')], codes)
    return data, namespace.methods


def make_dispatch():
    return _build(DISPATCH, 0x601, (), {'call': ('()V', 0x401)}, [], lambda _: {})


def make_receiver():
    return _build(RECEIVER, 1, (DISPATCH,), {'call': ('()V', 1)}, [],
                  lambda _: {'call': (1, 1, 0, [0x000e], 0)})


def write(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    result = {}
    for label, unsafe in [('safe', False), ('unsafe', True)]:
        data, methods = make(unsafe)
        (out / ('inline-' + label + '.dex')).write_bytes(data)
        result[label] = methods
    for label, filename, factory in [('interface', 'interface-dispatch.dex', make_dispatch),
                                     ('receiver', 'receiver-call.dex', make_receiver)]:
        data, namespace = factory()
        (out / filename).write_bytes(data)
        result[label] = namespace
    return result
