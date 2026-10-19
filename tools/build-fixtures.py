#!/usr/bin/env python3
"""Build single-class fixture DEX with verified, atomic per-variant caching."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unicodedata
import zlib
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[1]
# Java 9 supports private interface methods without Java 11 nest-host attributes,
# so independently compiled single-class DEX fixtures keep their existing boundary.
JAVAC_FLAGS = ['--release', '9', '-encoding', 'UTF-8', '-g']
# One DEX per class: RedefineClasses takes exactly one class per DEX, and a
# synthetic class (a new lambda) is a class to add, not part of its origin.
D8_FLAGS = ['--min-api', '30', '--file-per-class']
ENV_INPUTS = ['CLASSPATH', 'JAVA_TOOL_OPTIONS', '_JAVA_OPTIONS', 'JDK_JAVA_OPTIONS',
              'JDK_JAVAC_OPTIONS', 'JAVA_OPTS', 'LANG', 'LC_ALL', 'LC_CTYPE']


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def key(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def package_path(path):
    """Name of a source/class actually addressable from a classpath directory."""
    if path.suffix == '.java':
        raw = path.read_text()
        if '\\u' in raw: return None  # conservative Java Unicode-escape input
        text = re.sub(r"""/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'""", '', raw, flags=re.S)
        match = re.search(r'\bpackage\s+([\w.]+)\s*;', text)
        return (match[1].replace('.', '/') + '/' if match else '') + path.name
    data = path.read_bytes()
    if data[:4] != b'\xca\xfe\xba\xbe':
        return None
    count = struct.unpack_from('>H', data, 8)[0]
    values = {}; offset = 10; index = 1
    while index < count:
        tag = data[offset]; offset += 1
        if tag == 1:
            length = struct.unpack_from('>H', data, offset)[0]; offset += 2
            values[index] = data[offset:offset + length].decode('utf-8', errors='surrogatepass').encode('utf-16', errors='surrogatepass').decode('utf-16'); offset += length
        elif tag == 7:
            values[index] = struct.unpack_from('>H', data, offset)[0]; offset += 2
        elif tag in (3, 4, 9, 10, 11, 12, 17, 18): offset += 4
        elif tag in (5, 6): offset += 8; index += 1
        elif tag in (8, 16, 19, 20): offset += 2
        elif tag == 15: offset += 3
        else: return None
        index += 1
    return values[values[struct.unpack_from('>H', data, offset + 2)[0]]] + '.class'


def classpath_inputs(env, cwd):
    # javac's existing implicit source/class path is CLASSPATH, otherwise cwd.
    # Files buried under fixtures/<variant> or build/... are not package-relative
    # inputs of cwd; treating them as shared dependencies defeats variant caching.
    result = []
    for spelling in env.get('CLASSPATH', '.').split(os.pathsep):
        entry = Path(spelling or '.')
        if not entry.is_absolute(): entry = cwd / entry
        entries = sorted(entry.parent.glob('*.jar')) if entry.name == '*' else [entry]
        for actual in entries:
            if actual.is_file():
                result.append((str(actual.resolve()), digest(actual)))
                if zipfile.is_zipfile(actual):
                    with zipfile.ZipFile(actual) as jar:
                        if 'META-INF/services/javax.annotation.processing.Processor' in jar.namelist():
                            result.append(('uncached-annotation-processor', str(actual), uuid.uuid4().hex))
                continue
            if not actual.is_dir(): result.append((str(actual), None)); continue
            if (actual / 'META-INF/services/javax.annotation.processing.Processor').exists():
                result.append(('uncached-annotation-processor', str(actual), uuid.uuid4().hex))
            for base, directories, files in os.walk(actual, followlinks=False):
                # Directory symlinks can be compiler inputs too; hash their
                # target can contain arbitrary additional inputs. Refuse reuse for
                # this invocation while preserving the original compiler behavior.
                for name in list(directories):
                    candidate = Path(base) / name
                    if candidate.is_symlink():
                        result.append(('uncached-symlink-classpath', str(candidate), uuid.uuid4().hex))
                for name in files:
                    path = Path(base) / name
                    if path.suffix not in ('.java', '.class'): continue
                    relative = path.relative_to(actual).as_posix()
                    try: accessible = package_path(path)
                    except (ValueError, IndexError, KeyError, struct.error, UnicodeError): accessible = None
                    if accessible is None or unicodedata.normalize('NFC', accessible) == unicodedata.normalize('NFC', relative):
                        result.append((str(path), digest(path)))
    return sorted(result)


def common_inputs(java, sdk, env, cwd):
    tools = [java / 'bin/javac', java / 'bin/java', java / 'release',
             java / 'lib/modules', java / 'lib/tools.jar', java / 'lib/ct.sym', sdk / 'build-tools/35.0.0/d8',
             sdk / 'build-tools/35.0.0/d8.jar', sdk / 'build-tools/35.0.0/lib/d8.jar',
             sdk / 'platforms/android-35/android.jar', Path(__file__)]
    tools += [p for directory in (java / 'lib', java / 'conf') if directory.is_dir() for p in directory.rglob('*') if p.is_file()]
    return {'tools': [(str(p.resolve()), digest(p) if p.is_file() else None) for p in sorted(set(tools))],
            'javac': JAVAC_FLAGS, 'd8': D8_FLAGS,
            'environment': {name: env.get(name) for name in ENV_INPUTS},
            'classpath': classpath_inputs(env, cwd),
            # JVM/compiler options can name plugins, @files or boot paths not
            # declared by this builder. Preserve execution but refuse cache
            # reuse for such uncontrolled inputs rather than certify stale DEX.
            'uncontrolled-options': uuid.uuid4().hex if any(env.get(n) for n in ENV_INPUTS[1:6]) else None}


def outputs(directory):
    return {p.relative_to(directory).as_posix(): digest(p)
            for p in sorted(directory.rglob('*')) if p.is_file()}


def hit(state, name, fingerprint, directory):
    try: record = json.loads((state / (name + '.json')).read_text())
    except (OSError, ValueError): return False
    return (record.get('key') == fingerprint and bool(record.get('outputs'))
            and directory.is_dir() and record['outputs'] == outputs(directory))


def discard(out, state, name):
    destination = out / name
    if destination.is_symlink() or destination.is_file(): destination.unlink(missing_ok=True)
    else: shutil.rmtree(destination, ignore_errors=True)
    (state / (name + '.json')).unlink(missing_ok=True)


def publish(staged, out, state, name, fingerprint):
    record = {'key': fingerprint, 'outputs': outputs(staged)}
    if not record['outputs']: raise RuntimeError('No DEX produced for ' + name)
    backup = staged.parent / 'previous'
    destination = out / name
    if destination.exists(): destination.rename(backup)
    try:
        staged.rename(destination)
        temporary = state / (name + '.json.tmp')
        temporary.write_text(json.dumps(record, sort_keys=True))
        temporary.replace(state / (name + '.json'))
    except BaseException:
        discard(out, state, name)
        raise
    finally:
        shutil.rmtree(backup, ignore_errors=True)


def strip_class_methods(data, removals, access_changes=(), renames=None, class_access=None,
                        field_access_changes=()):
    """Remove declared classfile methods while preserving caller bytecode verbatim.

    This validates fixture identities and supports the standalone JVM probe.
    DEX generation retains the original declarations until D8 has chosen its
    invocation instructions. Every requested identity must occur exactly once.
    """
    offset = 0
    def take(size):
        nonlocal offset
        if size < 0 or offset + size > len(data): raise ValueError('Truncated classfile')
        start = offset; offset += size
        return data[start:offset]
    def number(size): return int.from_bytes(take(size), 'big')
    if take(4) != b'\xca\xfe\xba\xbe': raise ValueError('Invalid classfile magic')
    take(4)
    pool = {}; string_offsets = {}; count = number(2); index = 1
    while index < count:
        tag = number(1)
        if tag == 1:
            length = number(2); string_offsets[index] = (offset, length); pool[index] = take(length)
        elif tag in (3, 4, 9, 10, 11, 12, 17, 18): take(4)
        elif tag in (5, 6): take(8); index += 1
        elif tag in (7, 8, 16, 19, 20): take(2)
        elif tag == 15: take(3)
        else: raise ValueError('Unknown classfile constant tag')
        index += 1
    def attributes():
        for _ in range(number(2)):
            name = number(2)
            if name not in pool: raise ValueError('Invalid attribute name')
            take(number(4))
    def member():
        start = offset
        take(2); name = number(2); descriptor = number(2)
        if name not in pool or descriptor not in pool: raise ValueError('Invalid member identity')
        attributes()
        return (pool[name], pool[descriptor]), data[start:offset]
    class_flags_offset = offset
    take(6); take(number(2) * 2)
    fields_offset = offset
    fields = [member() for _ in range(number(2))]
    methods_offset = offset
    methods = [member() for _ in range(number(2))]
    trailing_offset = offset
    attributes()
    if offset != len(data): raise ValueError('Trailing classfile bytes')
    wanted = [(name.encode('utf-8'), descriptor.encode('utf-8')) for name, descriptor in removals]
    if len(set(wanted)) != len(wanted): raise ValueError('Duplicate method removal')
    for identity in wanted:
        if sum(candidate == identity for candidate, _ in methods) != 1:
            raise ValueError('Method removal did not identify exactly one declaration')
    def access_overrides(records, entries, removed=()):
        changes = {(name.encode('utf-8'), descriptor.encode('utf-8')): access
                   for name, descriptor, access in entries}
        if len(changes) != len(entries): raise ValueError('Duplicate classfile access change')
        if any(identity in removed or sum(candidate == identity for candidate, _ in records) != 1
               for identity in changes): raise ValueError('Classfile access change has no retained declaration')
        if any(type(access) is not int or not 0 <= access <= 0xffff for access in changes.values()):
            raise ValueError('Invalid classfile member access flags')
        return changes
    changes = access_overrides(methods, access_changes, wanted)
    field_changes = access_overrides(fields, field_access_changes)
    kept = [(struct.pack('>H', changes[identity]) + raw[2:] if identity in changes else raw)
            for identity, raw in methods if identity not in wanted]
    prefix = bytearray(data[:methods_offset])
    field_offset = fields_offset + 2
    for identity, raw in fields:
        if identity in field_changes: struct.pack_into('>H', prefix, field_offset, field_changes[identity])
        field_offset += len(raw)
    if class_access is not None:
        if (not isinstance(class_access, dict) or set(class_access) != {'clear', 'set'} or
                any(type(value) is not int or not 0 <= value <= 0xffff for value in class_access.values())):
            raise ValueError('Invalid classfile class access flags')
        flags = struct.unpack_from('>H', prefix, class_flags_offset)[0]
        struct.pack_into('>H', prefix, class_flags_offset,
                         (flags & ~class_access['clear']) | class_access['set'])
    for old, new in (renames or {}).items():
        if not isinstance(old, str) or not isinstance(new, str) or not old.isascii() or not new.isascii() or len(old) != len(new):
            raise ValueError('Classfile fixture names must have equal ASCII width')
        found = [index for index in string_offsets if pool[index] == old.encode('ascii')]
        if len(found) != 1 or new.encode('ascii') in pool.values():
            raise ValueError('Classfile rename must identify one unique constant')
        start, length = string_offsets[found[0]]
        prefix[start:start + length] = new.encode('ascii')
    return bytes(prefix) + struct.pack('>H', len(kept)) + b''.join(kept) + data[trailing_offset:]


class FixtureDex:
    """Read only the standard DEX metadata needed for fixture method removal."""
    def __init__(self, data):
        self.data = data
        if (len(data) < 0x70 or data[:4] != b'dex\n' or data[7] != 0
                or not b'035' <= data[4:7] <= b'040'
                or self.u32(0x20) != len(data) or self.u32(0x24) != 0x70
                or self.u32(0x28) != 0x12345678):
            raise ValueError('Invalid fixture DEX header')
        self.tables = {}
        for name, at, width in (('strings', 0x38, 4), ('types', 0x40, 4),
                                ('protos', 0x48, 12), ('methods', 0x58, 8), ('classes', 0x60, 32)):
            count, offset = self.u32(at), self.u32(at + 4)
            if (count == 0) != (offset == 0) or offset % 4:
                raise ValueError('Invalid fixture DEX table')
            self.range(offset, count * width)
            self.tables[name] = (offset, count, width)
        if self.tables['classes'][1] != 1:
            raise ValueError('Method removal needs a single-class DEX')
        definition = self.item('classes', 0)
        self.owner = self.type(self.u32(definition))
        self.class_data = self.u32(definition + 24)
        if not self.class_data: raise ValueError('Missing fixture class data')
        self.counts = []
        cursor = self.class_data
        for _ in range(4):
            count, cursor = self.leb(cursor); self.counts.append(count)
        fields_start = cursor
        for _ in range(self.counts[0] + self.counts[1]):
            _, cursor = self.leb(cursor); _, cursor = self.leb(cursor)
        self.fields = data[fields_start:cursor]
        self.groups = []
        self.declarations = {}
        for count in self.counts[2:]:
            group = []; index = 0
            for _ in range(count):
                difference, cursor = self.leb(cursor); index += difference
                access, cursor = self.leb(cursor); code, cursor = self.leb(cursor)
                identity = self.method(index)
                if identity in self.declarations: raise ValueError('Duplicate fixture method identity')
                self.declarations[identity] = (index, access, code)
                group.append((identity, index, access, code))
            self.groups.append(group)
        self.class_data_end = cursor

    def range(self, offset, size):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ValueError('Fixture DEX range outside file')
    def u16(self, at):
        self.range(at, 2); return struct.unpack_from('<H', self.data, at)[0]
    def u32(self, at):
        self.range(at, 4); return struct.unpack_from('<I', self.data, at)[0]
    def leb(self, at):
        value = 0
        for shift in range(0, 35, 7):
            self.range(at, 1); byte = self.data[at]; at += 1
            if shift == 28 and byte & 0xf0: raise ValueError('Invalid fixture ULEB128')
            value |= (byte & 0x7f) << shift
            if not byte & 0x80: return value, at
        raise ValueError('Unterminated fixture ULEB128')
    def item(self, name, index):
        offset, count, width = self.tables[name]
        if not 0 <= index < count: raise ValueError('Invalid fixture DEX index')
        return offset + index * width
    def string(self, index):
        offset = self.u32(self.item('strings', index)); _, offset = self.leb(offset)
        end = self.data.find(b'\0', offset)
        if end == -1: raise ValueError('Unterminated fixture DEX string')
        return self.data[offset:end].decode('utf-8')
    def type(self, index): return self.string(self.u32(self.item('types', index)))
    def method(self, index):
        at = self.item('methods', index)
        if self.type(self.u16(at)) != self.owner:
            raise ValueError('Fixture declaration belongs to another class')
        proto = self.item('protos', self.u16(at + 2))
        parameters = self.u32(proto + 8)
        types = []
        if parameters:
            for parameter in range(self.u32(parameters)):
                types.append(self.type(self.u16(parameters + 4 + 2 * parameter)))
        return self.string(self.u32(at + 4)), '(' + ''.join(types) + ')' + self.type(self.u32(proto + 4))


def strip_dex_methods(data, removals, access_changes=()):
    """Remove definitions or set fixture access flags after D8.

    D8 must see private declarations when it chooses invoke-direct. Removing a
    declaration before D8 silently changes that opcode to invoke-virtual. The
    shortened class_data item stays at its original offset; unused bytes/code
    items remain in the DEX and all referenced offsets retain their meaning.
    """
    dex = FixtureDex(data)
    wanted = [tuple(identity) for identity in removals]
    if len(set(wanted)) != len(wanted): raise ValueError('Duplicate DEX method removal')
    if any(identity not in dex.declarations for identity in wanted):
        raise ValueError('DEX method removal did not identify a declaration')
    overrides = {(name, descriptor): access for name, descriptor, access in access_changes}
    if len(overrides) != len(access_changes): raise ValueError('Duplicate DEX access change')
    if any(identity not in dex.declarations or identity in wanted for identity in overrides):
        raise ValueError('DEX access change did not identify a retained declaration')
    if any(type(access) is not int or access < 0 or access > 0xffff for access in overrides.values()):
        raise ValueError('Invalid DEX method access flags')
    groups = [[], []]
    result = bytearray(data)
    for group in dex.groups:
        for identity, index, access, code in group:
            if identity in wanted: continue
            replacement_access = overrides.get(identity, access)
            if (replacement_access ^ access) & 8 and code:
                # These fixtures change only an unused receiver to/from static.
                # Keep register/code offsets, adjusting incoming argument words.
                count = dex.u16(code + 2) + (-1 if replacement_access & 8 else 1)
                if count < 0: raise ValueError('Invalid fixture receiver word count')
                struct.pack_into('<H', result, code + 2, count)
            direct = bool(replacement_access & (8 | 2)) or identity[0] in ('<init>', '<clinit>')
            groups[0 if direct else 1].append((identity, index, replacement_access, code))
    for group in groups: group.sort(key=lambda record: record[1])
    def leb(value):
        result = bytearray()
        while value >= 0x80:
            result.append((value & 0x7f) | 0x80); value >>= 7
        result.append(value)
        return bytes(result)
    replacement = b''.join(leb(count) for count in (*dex.counts[:2], *map(len, groups))) + dex.fields
    for group in groups:
        previous = 0
        for _, index, access, code in group:
            replacement += leb(index - previous) + leb(access) + leb(code)
            previous = index
    length = dex.class_data_end - dex.class_data
    if len(replacement) > length: raise ValueError('Fixture class data unexpectedly grew')
    result[dex.class_data:dex.class_data_end] = replacement + bytes(length - len(replacement))
    result[12:32] = hashlib.sha1(result[32:]).digest()
    struct.pack_into('<I', result, 8, zlib.adler32(result[12:]) & 0xffffffff)
    return bytes(result)


def rename_dex_strings(data, renames):
    """Craft inherited/Object hiding that javac correctly refuses to express.

    Equal-width spelling changes preserve every offset; enforce DEX's sorted
    unique string table as well as its checksums instead of emitting a corrupt
    file that would be rejected before the policy under test.
    """
    dex = FixtureDex(data)
    strings = [dex.string(index) for index in range(dex.u32(56))]
    replacements = [renames.get(value, value) for value in strings]
    if replacements != sorted(set(replacements)): raise ValueError('Renamed strings are not sorted/unique')
    result = bytearray(data)
    for old, new in renames.items():
        if old not in strings or not old.isascii() or not new.isascii() or len(old) != len(new):
            raise ValueError('Invalid equal-width DEX string replacement')
        offset = dex.u32(dex.item('strings', strings.index(old)))
        _, offset = dex.leb(offset)
        result[offset:offset + len(old)] = new.encode('ascii')
    result[12:32] = hashlib.sha1(result[32:]).digest()
    struct.pack_into('<I', result, 8, zlib.adler32(result[12:]) & 0xffffffff)
    return bytes(result)


def compile_variant(variant, sources, java, sdk, env, staged, work):
    classes = work / 'classes'; classes.mkdir()
    subprocess.run([str(java / 'bin/javac'), *JAVAC_FLAGS, '-d', str(classes),
                    *map(str, sources)], check=True, env=env)
    recipe = variant / 'remove-methods.json'
    removals = {}
    access_recipe = variant / 'method-access.json'
    access_changes = json.loads(access_recipe.read_text()) if access_recipe.exists() else {}
    rename_recipe = variant / 'rename-strings.json'
    renames = json.loads(rename_recipe.read_text()) if rename_recipe.exists() else {}
    if not isinstance(access_changes, dict) or not isinstance(renames, dict):
        raise ValueError('Invalid DEX fixture recipe')
    for relative, changes in access_changes.items():
        if not isinstance(relative, str) or not (classes / relative).is_file() or not isinstance(changes, list):
            raise ValueError('Access recipe must name a compiled class and method list')
        if any(not isinstance(change, list) or len(change) != 3 for change in changes):
            raise ValueError('Invalid access recipe entry')
    if recipe.exists():
        removals = json.loads(recipe.read_text())
        if not isinstance(removals, dict): raise ValueError('Invalid method removal recipe')
        for relative, identities in removals.items():
            path = Path(relative)
            if (path.is_absolute() or '..' in path.parts or path.suffix != '.class'
                    or not isinstance(identities, list)
                    or any(not isinstance(identity, list) or len(identity) != 2
                           or any(not isinstance(value, str) or not value for value in identity)
                           for identity in identities)):
                raise ValueError('Invalid method removal recipe entry')
            klass = classes / path
            # Validate exact classfile identities but feed the intact private
            # declarations to D8 so its invocation opcode decisions stay valid.
            strip_class_methods(klass.read_bytes(), identities)
    class_recipe = variant / 'class-method-access.json'
    class_changes = json.loads(class_recipe.read_text()) if class_recipe.exists() else {}
    if not isinstance(class_changes, dict): raise ValueError('Invalid classfile access recipe')
    for relative, changes in class_changes.items():
        path = Path(relative)
        if path.is_absolute() or '..' in path.parts or path.suffix != '.class' or not isinstance(changes, list):
            raise ValueError('Invalid classfile access recipe entry')
        if any(not isinstance(change, list) or len(change) not in (3, 4) for change in changes):
            raise ValueError('Invalid classfile method access/name entry')
        # The optional fourth value gives a same-width constant-pool spelling.
        # D8 recognizes javac's lambda$ naming and widens those helpers; Kotlin
        # uses value$lambda$ and keeps them private behind generated bridges.
        class_renames = {change[0]: change[3] for change in changes if len(change) == 4}
        klass = classes / path
        klass.write_bytes(strip_class_methods(klass.read_bytes(), [], [change[:3] for change in changes], class_renames))
    field_recipe = variant / 'class-field-access.json'
    field_changes = json.loads(field_recipe.read_text()) if field_recipe.exists() else {}
    if not isinstance(field_changes, dict): raise ValueError('Invalid classfile field access recipe')
    for relative, changes in field_changes.items():
        path = Path(relative)
        if (path.is_absolute() or '..' in path.parts or path.suffix != '.class' or not isinstance(changes, list)
                or any(not isinstance(change, list) or len(change) != 3 for change in changes)):
            raise ValueError('Invalid classfile field access recipe entry')
        klass = classes / path
        klass.write_bytes(strip_class_methods(klass.read_bytes(), [], field_access_changes=changes))
    kind_recipe = variant / 'class-access.json'
    kind_changes = json.loads(kind_recipe.read_text()) if kind_recipe.exists() else {}
    if not isinstance(kind_changes, dict): raise ValueError('Invalid classfile class access recipe')
    for relative, flags in kind_changes.items():
        path = Path(relative)
        if path.is_absolute() or '..' in path.parts or path.suffix != '.class':
            raise ValueError('Invalid classfile class access recipe entry')
        klass = classes / path
        klass.write_bytes(strip_class_methods(klass.read_bytes(), [], class_access=flags))
    for index, klass in enumerate(sorted(classes.rglob('*.class'))):
        dexdir = work / ('dex-' + str(index)); dexdir.mkdir()
        subprocess.run([str(sdk / 'build-tools/35.0.0/d8'), *D8_FLAGS, '--lib',
                        str(sdk / 'platforms/android-35/android.jar'), '--output',
                        str(dexdir), str(klass)], check=True, env=env)
        own = klass.relative_to(classes).with_suffix('.dex')
        produced = sorted(path.relative_to(dexdir) for path in dexdir.rglob('*.dex'))
        if own not in produced: raise RuntimeError('Missing DEX for ' + str(klass))
        for relative in produced:
            dest = staged / relative
            if dest.exists(): raise RuntimeError('Duplicate DEX ' + relative.as_posix())
            dest.parent.mkdir(parents=True, exist_ok=True)
            data = (dexdir / relative).read_bytes()
            if not data: raise RuntimeError('Empty DEX ' + relative.as_posix())
            identities = removals.get(klass.relative_to(classes).as_posix()) if relative == own else None
            changes = access_changes.get(klass.relative_to(classes).as_posix(), []) if relative == own else []
            if identities or changes: data = strip_dex_methods(data, identities or [], changes)
            if renames and any(old.encode('ascii') + b'\0' in data for old in renames):
                data = rename_dex_strings(data, {old: new for old, new in renames.items()
                                                if old.encode('ascii') + b'\0' in data})
            if variant.name == 'interface-invalid-invoke' and relative == own:
                data = invalid_interface_invoke(data)
            dest.write_bytes(data)


def invalid_interface_invoke(data):
    """Keep a valid DEX encoding but give String.length() an integer receiver."""
    dex = FixtureDex(data)
    _, _, code = dex.declarations[('copiedValue', '()I')]
    start = code + 16
    # D8 emits const-string vN, string@X; invoke-virtual {vN}, String.length.
    # Both const-string and const/16 occupy two code units. Fail closed if the
    # compiler changes this fixture rather than corrupting arbitrary code.
    dex.range(start, 10)
    if (dex.u32(code + 12) < 5 or data[start] != 0x1a or data[start + 4] != 0x6e
            or data[start + 5] != 0x10 or dex.u16(start + 8) != data[start + 1]):
        raise ValueError('Unexpected invalid-invoke fixture instructions')
    method = dex.item('methods', dex.u16(start + 6))
    if (dex.type(dex.u16(method)) != 'Ljava/lang/String;'
            or dex.string(dex.u32(method + 4)) != 'length'):
        raise ValueError('Unexpected invalid-invoke receiver method')
    result = bytearray(data)
    result[start] = 0x13  # const/16 vN, #1 instead of const-string vN.
    struct.pack_into('<H', result, start + 2, 1)
    result[12:32] = hashlib.sha1(result[32:]).digest()
    struct.pack_into('<I', result, 8, zlib.adler32(result[12:]) & 0xffffffff)
    return bytes(result)


def invalid_hierarchy(source):
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
    return bytes(data)


def build(sdk, java, out, fixtures=ROOT / 'fixtures'):
    sdk = sdk.resolve(); java = java.resolve(); out = out.resolve(); fixtures = fixtures.resolve()
    out.mkdir(parents=True, exist_ok=True)
    state = out.parent / ('.' + out.name + '-fixture-cache'); state.mkdir(exist_ok=True)
    env = dict(os.environ, JAVA_HOME=str(java)); env['PATH'] = str(java / 'bin') + os.pathsep + env['PATH']
    with (state / 'lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        common = common_inputs(java, sdk, env, Path.cwd())
        variants = [(p, sorted(p.rglob('*.java'))) for p in sorted(fixtures.iterdir()) if p.is_dir()]
        variants = [(p, sources) for p, sources in variants if sources]
        live = {p.name for p, _ in variants}
        # This directory is exclusively generated patch assets, including the
        # one derived variant. Removed variants must not survive a warm build.
        for path in out.iterdir():
            if path.is_dir() and path.name not in live | {'hier-invalid'}: discard(out, state, path.name)
        for manifest in state.glob('*.json'):
            if manifest.stem not in live | {'hier-invalid'}: manifest.unlink()
        for variant, sources in variants:
            recipes = [variant / name for name in ('remove-methods.json', 'method-access.json', 'rename-strings.json', 'class-method-access.json', 'class-field-access.json', 'class-access.json')]
            inputs = sources + [recipe for recipe in recipes if recipe.exists()]
            fingerprint = key({'common': common, 'sources': [(p.relative_to(variant).as_posix(), digest(p)) for p in inputs]})
            if hit(state, variant.name, fingerprint, out / variant.name):
                print('CACHED ' + variant.name); continue
            try:
                with tempfile.TemporaryDirectory(prefix='fixture-stage-', dir=state) as temp:
                    work = Path(temp); staged = work / 'assets'; staged.mkdir()
                    compile_variant(variant, sources, java, sdk, env, staged, work)
                    publish(staged, out, state, variant.name, fingerprint)
                print('BUILT ' + variant.name)
            except BaseException:
                discard(out, state, variant.name)
                if variant.name == 'hier-parent': discard(out, state, 'hier-invalid')
                raise
        relative = 'com/github/ytlog/artdcevm/fixture/HierarchySubject.dex'
        source = out / 'hier-parent' / relative
        if 'hier-parent' not in live or not source.is_file(): discard(out, state, 'hier-invalid')
        else:
            fingerprint = key({'recipe': digest(Path(__file__)), 'parent': digest(source)})
            if hit(state, 'hier-invalid', fingerprint, out / 'hier-invalid'): print('CACHED hier-invalid')
            else:
                try:
                    with tempfile.TemporaryDirectory(prefix='fixture-derived-', dir=state) as temp:
                        staged = Path(temp) / 'assets'; dest = staged / relative; dest.parent.mkdir(parents=True)
                        dest.write_bytes(invalid_hierarchy(source))
                        publish(staged, out, state, 'hier-invalid', fingerprint)
                    print('BUILT hier-invalid')
                except BaseException:
                    discard(out, state, 'hier-invalid'); raise


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--sdk', required=True, type=Path)
    p.add_argument('--java', required=True, type=Path)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--fixtures', type=Path, default=ROOT / 'fixtures', help='Fixture source root (isolated build verification)')
    a = p.parse_args(); build(a.sdk, a.java, a.out, a.fixtures)


if __name__ == '__main__': main()
