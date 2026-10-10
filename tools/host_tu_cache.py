"""Content-checked host TU cache; discover actual headers before every lookup."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def dependencies(path):
    # clang Make dependencies escape spaces and fold lines with backslash-newline.
    text = path.read_text().replace('\\\n', '')
    return sorted({str(Path(p).resolve()) for p in shlex.split(text.split(':', 1)[1])})


def compile_cached(base, sources, directory, max_workers=2):
    started = time.monotonic()
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    compiler = Path(shutil.which(base[0]) or base[0]).resolve()
    identity = {'path': str(compiler), 'sha256': digest(compiler),
                'version': subprocess.check_output([str(compiler), '--version'], text=True),
                'environment': {name: os.environ.get(name) for name in (
                    'SDKROOT', 'MACOSX_DEPLOYMENT_TARGET', 'CPATH',
                    'CPLUS_INCLUDE_PATH', 'C_INCLUDE_PATH', 'OBJC_INCLUDE_PATH',
                    'LIBRARY_PATH', 'COMPILER_PATH', 'GCC_EXEC_PREFIX')}}
    config = json.dumps({'compiler': identity, 'flags': base}, sort_keys=True).encode()

    def one(source):
        source = Path(source).resolve()
        stem = source.stem + '-' + hashlib.sha256(str(source).encode()).hexdigest()[:12]
        with tempfile.TemporaryDirectory(prefix=stem + '-', dir=directory) as temporary:
            stage = Path(temporary)
            dep = stage / 'probe.d'
            # Actual preprocessing detects new/deleted optional headers and
            # include-search changes as well as transitive content changes.
            subprocess.run(base + ['-M', '-MT', 'object', '-MF', str(dep), str(source)], check=True)
            inputs = {p: digest(p) for p in dependencies(dep)}
            key = hashlib.sha256(config + json.dumps(inputs, sort_keys=True).encode()).hexdigest()
            target = directory / (stem + '-' + key + '.o')
            manifest = directory / (stem + '-' + key + '.json')
            try:
                saved = json.loads(manifest.read_text())
                if saved['key'] == key and saved['object_sha256'] == digest(target):
                    return str(target), False
            except (OSError, ValueError, KeyError):
                pass
            obj = stage / 'object.o'
            compiled_dep = stage / 'compiled.d'
            command = base + ['-MD', '-MF', str(compiled_dep), '-c', str(source), '-o', str(obj)]
            result = subprocess.run(command, capture_output=True, text=True)
            (directory / (stem + '.log')).write_text(result.stdout + result.stderr)
            if result.returncode:
                print(result.stdout + result.stderr)
            result.check_returncode()
            actual = {p: digest(p) for p in dependencies(compiled_dep)}
            if inputs != actual:
                raise RuntimeError('Host TU inputs changed during compilation: ' + str(source))
            metadata = stage / 'manifest.json'
            metadata.write_text(json.dumps({'key': key, 'inputs': actual,
                                            'object_sha256': digest(obj), 'command': command}, indent=2))
            # Only successful, input-stable output is published. A failing TU
            # aborts the caller; an old object can never stand in for its failure.
            os.replace(obj, target)
            os.replace(metadata, manifest)
            return str(target), True

    with ThreadPoolExecutor(max_workers=min(2, max_workers)) as pool:
        results = list(pool.map(one, sources))
    (directory / 'last-run.json').write_text(json.dumps({
        'seconds': time.monotonic() - started,
        'compiled': sum(changed for _, changed in results),
        'reused': sum(not changed for _, changed in results),
        'objects': [p for p, _ in results]}, indent=2))
    return [p for p, _ in results]
