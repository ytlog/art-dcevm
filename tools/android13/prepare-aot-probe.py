#!/usr/bin/env python3
"""Build a debuggable AOT sidecar for the target-phone loader diagnostic."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--serial', required=True)
parser.add_argument('--non-debuggable', action='store_true',
                    help='Compile an isolated optimized sidecar without the debuggable flag')
variant_group = parser.add_mutually_exclusive_group()
variant_group.add_argument('--active-frame', action='store_true',
                           help='Prepare the blocking AotActiveSubject sidecar')
variant_group.add_argument('--interface-removal', action='store_true',
                           help='Prepare the blocking AOT invoke-interface sidecar')
variant_group.add_argument('--parent-swap', action='store_true',
                           help='Prepare the blocking AOT superclass replacement sidecar')
args = parser.parse_args()

variant = ('aot-swap-original' if args.parent_swap else
           'aot-interface-original' if args.interface_removal else
           'aot-active-original' if args.active_frame else 'loader-original')
klass = ('AotSwapSubject' if args.parent_swap else
         'AotInterfaceSubject' if args.interface_removal else
         'AotActiveSubject' if args.active_frame else 'LoaderSubject')
stem = ('artdcevm-aot-swap' if args.parent_swap else
        'artdcevm-aot-interface' if args.interface_removal else
        'artdcevm-aot-active' if args.active_frame else 'artdcevm-aot')
if args.non_debuggable:
    stem += '-optimized'
source = root / f'app/build/generated/patchAssets/{variant}/dev/artdcevm/gc/{klass}.dex'
if not source.is_file():
    parser.error('Build patch fixtures before preparing the AOT probe')

out = root / 'evidence/device-aot' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
out.mkdir(parents=True)
if args.active_frame or args.interface_removal or args.parent_swap:
    extra_classes = (['AotSwapParent', 'AotSwapAlternate', 'AotSwapCaller'] if args.parent_swap
                     else ['AotInterfaceTag', 'AotInterfaceCaller'] if args.interface_removal
                     else ['AotActiveCaller'])
    extras = [source.with_name(name + '.dex') for name in extra_classes]
    if not all(path.is_file() for path in extras):
        parser.error('Build the caller and interface fixtures before preparing the AOT probe')
    sdk = next(line.split('=', 1)[1] for line in (root / 'local.properties').read_text().splitlines()
               if line.startswith('sdk.dir='))
    merged = out / 'merged'
    merged.mkdir()
    subprocess.run([str(Path(sdk) / 'build-tools/35.0.0/d8'), '--min-api', '33',
                    '--output', str(merged), str(source), *(str(path) for path in extras)], check=True)
    source = merged / 'classes.dex'
adb = ['adb', '-s', args.serial]
remote = '/data/local/tmp/' + stem
private = '/data/user/0/dev.artdcevm.lab/files/' + stem + '.dex'
record = {'serial': args.serial, 'variant': variant,
          'nonDebuggable': args.non_debuggable,
          'dexSha256': hashlib.sha256(source.read_bytes()).hexdigest(),
          'dexLocation': private, 'steps': []}

def run(*arguments):
    result = subprocess.run([*adb, *arguments], capture_output=True, text=True, timeout=120)
    record['steps'].append({'args': list(arguments), 'exitCode': result.returncode,
                            'stdout': result.stdout, 'stderr': result.stderr})
    result.check_returncode()
    return result.stdout + result.stderr

try:
    run('shell', 'run-as', 'dev.artdcevm.lab', 'id')
    run('push', str(source), remote + '.dex')
    command = ['shell', '/apex/com.android.art/bin/dex2oat64',
               '--dex-file=' + remote + '.dex', '--dex-location=' + private,
               '--oat-file=' + remote + '.odex', '--instruction-set=arm64',
               '--compiler-filter=speed']
    if not args.non_debuggable:
        command.append('--debuggable')
    command += ['--class-loader-context=PCL[]', '--runtime-arg', '-Xms64m',
                '--runtime-arg', '-Xmx256m']
    run(*command)
    header = run('shell', '/apex/com.android.art/bin/oatdump',
                 '--oat-file=' + remote + '.odex', '--header-only')
    listing = run('shell', '/apex/com.android.art/bin/oatdump',
                  '--oat-file=' + remote + '.odex')
    (out / 'oatdump.txt').write_text(listing)
    markers = (['int dev.artdcevm.gc.AotSwapCaller.call()'] if args.parent_swap else
               ['int dev.artdcevm.gc.AotInterfaceCaller.call()',
                'int dev.artdcevm.gc.AotInterfaceCaller.callDefault()'] if args.interface_removal else
               ['int dev.artdcevm.gc.AotActiveSubject.value()',
                'int dev.artdcevm.gc.AotActiveCaller.call(dev.artdcevm.gc.AotActiveSubject)'])
    required_methods_compiled = (args.active_frame or args.interface_removal or args.parent_swap) and all(
        marker in listing and 'code_offset:' in listing.split(marker, 1)[1].split('\n\n', 1)[0]
        for marker in markers)
    expected_debuggable = 'debuggable = false' if args.non_debuggable else 'debuggable = true'
    if 'compiler-filter = speed' not in header or expected_debuggable not in header \
            or klass + '; (offset=' not in listing \
            or not (required_methods_compiled if (args.active_frame or args.interface_removal or args.parent_swap)
                    else '(AllCompiled)' in listing):
        raise ValueError('dex2oat did not produce the expected debuggable compiled class')
    run('shell', 'run-as', 'dev.artdcevm.lab', 'mkdir', '-p', 'files/oat/arm64')
    for src, dst in ((remote + '.dex', 'files/' + stem + '.dex'),
                     (remote + '.odex', 'files/oat/arm64/' + stem + '.odex'),
                     (remote + '.vdex', 'files/oat/arm64/' + stem + '.vdex')):
        run('shell', 'run-as', 'dev.artdcevm.lab', 'cp', src, dst)
    record['prepared'] = True
except (subprocess.CalledProcessError, subprocess.TimeoutExpired, ValueError) as error:
    record['prepared'] = False
    record['error'] = str(error)

(out / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps({'prepared': record['prepared'], 'evidence': str(out)}, indent=2))
raise SystemExit(0 if record['prepared'] else 1)
