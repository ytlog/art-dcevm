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
JAVAC_FLAGS = ['--release', '8', '-encoding', 'UTF-8', '-g']
D8_FLAGS = ['--min-api', '30']
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


def compile_variant(variant, sources, java, sdk, env, staged, work):
    classes = work / 'classes'; classes.mkdir()
    subprocess.run([str(java / 'bin/javac'), *JAVAC_FLAGS, '-d', str(classes),
                    *map(str, sources)], check=True, env=env)
    for index, klass in enumerate(sorted(classes.rglob('*.class'))):
        dexdir = work / ('dex-' + str(index)); dexdir.mkdir()
        subprocess.run([str(sdk / 'build-tools/35.0.0/d8'), *D8_FLAGS, '--lib',
                        str(sdk / 'platforms/android-35/android.jar'), '--output',
                        str(dexdir), str(klass)], check=True, env=env)
        dest = staged / klass.relative_to(classes).with_suffix('.dex')
        dest.parent.mkdir(parents=True, exist_ok=True)
        data = (dexdir / 'classes.dex').read_bytes()
        if not data: raise RuntimeError('Empty DEX for ' + str(klass))
        dest.write_bytes(data)


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
            fingerprint = key({'common': common, 'sources': [(p.relative_to(variant).as_posix(), digest(p)) for p in sources]})
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
