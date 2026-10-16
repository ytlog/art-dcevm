#!/usr/bin/env python3
"""Verify normal AARs exclude abort tests and the lab APK uses its own engine."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import zipfile

from evidence_paths import new_evidence_directory

ROOT = Path(__file__).resolve().parents[1]
TOKENS = ('InterfaceAbortInstrumentation', 'InjectInstanceFailure', 'InjectDispatchProbeFailure', 'InjectMappingPlanFailure', 'failure_stage',
          'failure_hits', 'discarded_classes', 'Inject(int)')
EXPORTS = ('configureCheckJni', 'armFailure', 'discardedClasses', 'failureHits', 'allocationOrder', 'allocatedTypes', 'primePointerId', 'validatedInterfaceGroups', 'rejectRuntimeThreadProbe', 'runtimeReferenceReads', 'runtimeMarkingReads')


def ndk_bin():
    properties = dict(line.split('=', 1) for line in (ROOT / 'local.properties').read_text().splitlines()
                      if '=' in line and not line.startswith('#'))
    prebuilt = Path(properties['sdk.dir']) / 'ndk/27.2.12479018/toolchains/llvm/prebuilt'
    return next(path / 'bin' for path in prebuilt.iterdir() if (path / 'bin/llvm-nm').is_file())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk-bin', type=Path)
    parser.add_argument('--evidence-root', type=Path, default=ROOT / 'build/evidence/abort-isolation')
    args = parser.parse_args()
    if not args.evidence_root.resolve().is_relative_to((ROOT / 'build/evidence').resolve()):
        parser.error('Evidence must stay under ignored build/evidence')
    out = new_evidence_directory(args.evidence_root.resolve())
    bins = args.ndk_bin or ndk_bin()

    def capture(label, tool, *arguments):
        result = subprocess.run([str(bins / tool), *map(str, arguments)], capture_output=True,
                                text=True, timeout=120)
        output = result.stdout + result.stderr
        (out / (label + '.txt')).write_text(output)
        if result.returncode and 'no symbols' not in output:
            raise ValueError('LLVM inspection failed for ' + label)
        return output

    def build_id(path, label):
        value = capture(label + '-notes', 'llvm-readelf', '-n', path)
        match = re.search(r'Build ID:\s*([a-fA-F0-9]+)', value)
        if not match:
            raise ValueError('Missing ELF Build ID for ' + label)
        return match[1]

    results = []
    artifacts = (
        ('library-debug', ROOT / 'artdcevm/build/outputs/aar/artdcevm-debug.aar',
         'jni/arm64-v8a/libartdcevm.so', 'artdcevm', 'Debug', False),
        ('library-release', ROOT / 'artdcevm/build/outputs/aar/artdcevm-release.aar',
         'jni/arm64-v8a/libartdcevm.so', 'artdcevm', 'Release', False),
        ('app-debug', ROOT / 'app/build/outputs/apk/debug/app-debug.apk',
         'lib/arm64-v8a/libartdcevm.so', 'app', 'Debug', True))
    for name, artifact, entry, module, mode, tests in artifacts:
        with zipfile.ZipFile(artifact) as archive:
            binary = out / (name + '.so')
            binary.write_bytes(archive.read(entry))
        identity = build_id(binary, name)
        modes = ('Release', 'RelWithDebInfo') if mode == 'Release' else (mode,)
        candidates = sorted(path for configuration in modes
                            for path in (ROOT / module / 'build/intermediates/cxx' / configuration).glob(
                                '*/obj/arm64-v8a/libartdcevm.so'))
        unstripped = next((path for number, path in enumerate(candidates)
                          if build_id(path, name + '-candidate-' + str(number)) == identity), None)
        if not unstripped:
            raise ValueError(name + ' does not match a module-owned unstripped build')
        dynamic = capture(name + '-dynamic', 'llvm-nm', '-D', '--defined-only', '--demangle', binary)
        symbols = capture(name + '-unstripped', 'llvm-nm', '--defined-only', '--demangle', unstripped)
        if tests:
            if not all('InterfaceAbortInstrumentation_' + export in dynamic for export in EXPORTS):
                raise ValueError('The lab APK is missing required test JNI exports')
            if not all(token in symbols for token in TOKENS):
                raise ValueError('The app engine is missing abort-test implementation symbols')
        elif any(token in dynamic or token in symbols for token in TOKENS):
            raise ValueError(name + ' contains interface abort-test symbols')
        results.append({'artifact': name, 'artifactSha256': hashlib.sha256(artifact.read_bytes()).hexdigest(),
                        'buildId': identity, 'testSymbols': tests, 'matchedUnstripped': True})
    (out / 'summary.json').write_text(json.dumps({'passed': True, 'artifacts': results}, indent=2))
    print(json.dumps({'passed': True, 'artifacts': results}, indent=2))
    print('Evidence: ' + str(out.relative_to(ROOT)))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except ValueError as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
    except (OSError, KeyError, StopIteration, zipfile.BadZipFile, subprocess.TimeoutExpired):
        print('Unable to read artifacts or run LLVM tools; build all three artifacts first', file=sys.stderr)
        sys.exit(1)
