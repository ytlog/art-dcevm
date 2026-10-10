#!/usr/bin/env python3
"""Check tracked files or a selected Git history before publishing.

Findings report paths and line numbers, never the private values themselves.
"""
import argparse
import bisect
import re
import subprocess
import sys


def git(*args):
    return subprocess.check_output(['git', *args])


HOME = re.compile(rb'(?:/Users/|/home/)[A-Za-z][A-Za-z0-9._-]*/')
IP = re.compile(rb'\b(?:192\.168\.\d{1,3}\.\d{1,3}|10\.\d{1,3}\.\d{1,3}\.\d{1,3}|172\.(?:1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3})\b')
SERIAL = re.compile(rb'(?i)(?:--serial|adb\s+-s|"(?:serial|deviceSerial|adb_serial)"\s*:)\s*["\x27]?([a-z0-9][a-z0-9._:-]{5,})')
PLACEHOLDERS = (b'device', b'serial', b'frozen', b'pixel', b'android',
                b'emulator', b'target', b'localhost', b'your_',
                b'adb_endpoint', b'private_adb_host', b'127.')


def findings(data):
    if b'\0' in data[:4096]:
        return []
    result = [('local user path', m.start()) for m in HOME.finditer(data)]
    result += [('private network address', m.start()) for m in IP.finditer(data)
               if m.group() not in (b'10.0.2.2', b'10.0.2.3')]
    for match in SERIAL.finditer(data):
        value = match.group(1)
        if value.lower().startswith(PLACEHOLDERS):
            continue
        if re.fullmatch(rb'[A-Za-z0-9-]{6,}', value) and re.search(rb'\d', value):
            result.append(('device identifier', match.start()))
    if not result:
        return []
    lines = [m.start() for m in re.finditer(b'\n', data)]
    return [(category, bisect.bisect_left(lines, offset) + 1)
            for category, offset in result]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--history', action='store_true', help='scan the selected reachable history')
    parser.add_argument('--ref', default='HEAD', help='history root; defaults to HEAD')
    parser.add_argument('--staged', action='store_true', help='scan the index rather than working files')
    args = parser.parse_args()
    if args.history and args.staged:
        parser.error('--history and --staged cannot be combined')
    failures = []
    if args.history:
        objects = git('rev-list', '--objects', args.ref).decode().splitlines()
        paths = {s.split(' ', 1)[0]: s.split(' ', 1)[1] for s in objects if ' ' in s}
        check = subprocess.check_output(['git', 'cat-file', '--batch-check'],
                                        input=('\n'.join(paths) + '\n').encode()).decode().splitlines()
        blobs = [s.split()[0] for s in check if s.split()[1] == 'blob']
        proc = subprocess.Popen(['git', 'cat-file', '--batch'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        for oid in blobs:
            proc.stdin.write((oid + '\n').encode()); proc.stdin.flush()
            size = int(proc.stdout.readline().split()[2])
            data = proc.stdout.read(size); proc.stdout.read(1)
            failures += [(paths[oid], line, category) for category, line in findings(data)]
        proc.stdin.close(); proc.wait()
        failures += [(p, 0, 'raw evidence in history') for p in set(paths.values()) if p.startswith('evidence/')]
        count = len(blobs)
    else:
        paths = git('ls-files', '-z').decode().split('\0')[:-1]
        for path in paths:
            data = git('show', ':' + path) if args.staged else open(path, 'rb').read()
            failures += [(path, line, category) for category, line in findings(data)]
        count = len(paths)
    for path, line, category in failures[:20]:
        print(f'{path}:{line}: {category}', file=sys.stderr)
    print(f'{"FAIL" if failures else "PASS"}: {count} files/blobs, {len(failures)} privacy findings')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
