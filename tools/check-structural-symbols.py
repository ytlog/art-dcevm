#!/usr/bin/env python3
"""Verify the structural-redefinition symbol catalog against collected ART libraries.

Every NamedSymbol in platform/symbols/structural_redefine_symbols.h must be a
defined name in its declared ELF symbol tables in each runtime set under
assets/runtime_lib. No address or disassembly is used. Exit status is non-zero
when any set misses a symbol.
"""
import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path
from elf_mini_inventory import mini_symbol_inventory

ROOT = Path(__file__).resolve().parent.parent
CATALOG = ROOT / 'artdcevm/src/main/cpp/platform/symbols/structural_redefine_symbols.h'
LIBRARIES = {'kJvmti': 'libopenjdkjvmti.so', 'kArt': 'libart.so'}
ENTRY = re.compile(r'NamedSymbol\s+(k\w+)\{Library::(k\w+),\s*"([^"]+)"'
                   r'(?:,\s*SymbolTable::(k\w+))?\s*\}')


def find_nm(explicit):
    if explicit:
        return explicit
    found = shutil.which('llvm-nm')
    if found:
        return Path(found)
    sdk = None
    properties = ROOT / 'local.properties'
    if properties.exists():
        for line in properties.read_text().splitlines():
            if line.startswith('sdk.dir='):
                sdk = Path(line.split('=', 1)[1].strip())
    if sdk:
        candidates = sorted(sdk.glob('ndk/*/toolchains/llvm/prebuilt/*/bin/llvm-nm'))
        if candidates:
            return candidates[-1]
    sys.exit('llvm-nm not found; pass --nm or set sdk.dir in local.properties')


def defined_symbols(nm, library, dynamic):
    command = [str(nm), *(['-D'] if dynamic else []), '--defined-only', str(library)]
    output = subprocess.run(command,
                            check=True, capture_output=True, text=True).stdout
    return {fields[-1] for fields in (line.split() for line in output.splitlines()) if fields}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nm', type=Path)
    parser.add_argument('--runtime-lib', type=Path, default=ROOT / 'assets/runtime_lib')
    args = parser.parse_args()

    catalog = ENTRY.findall(CATALOG.read_text())
    if not catalog:
        sys.exit(f'no NamedSymbol entries parsed from {CATALOG.relative_to(ROOT)}')
    unknown = {library for _, library, _, _ in catalog} - LIBRARIES.keys()
    if unknown:
        sys.exit(f'catalog uses unsupported libraries: {sorted(unknown)}')
    tables = {table for _, _, _, table in catalog} - {'', 'kDynsym', 'kAnyNamed'}
    if tables:
        sys.exit(f'catalog uses unsupported symbol tables: {sorted(tables)}')

    nm = find_nm(args.nm)
    sets = sorted(path.parent for path in args.runtime_lib.rglob('libopenjdkjvmti.so'))
    if not sets:
        sys.exit(f'no runtime sets under {args.runtime_lib}')

    failed = 0
    for directory in sets:
        label = directory.relative_to(args.runtime_lib).parts[:3]
        missing = []
        cache = {}
        for name, library, symbol, table in catalog:
            path = directory / LIBRARIES[library]
            key = (library, table or 'kDynsym')
            if key not in cache:
                names = defined_symbols(nm, path, dynamic=True) if path.exists() else set()
                if table == 'kAnyNamed' and path.exists():
                    names.update(defined_symbols(nm, path, dynamic=False))
                    mini_names, _ = mini_symbol_inventory(path.read_bytes())
                    names.update(mini_names)
                cache[key] = names
            if symbol not in cache[key]:
                missing.append(f'{LIBRARIES[library]}:{name}')
        status = 'ok' if not missing else 'missing ' + ', '.join(missing)
        print(f"{'/'.join(label)}: {status}")
        failed += bool(missing)

    local = sum(table == 'kAnyNamed' for _, _, _, table in catalog)
    print(f'{len(sets) - failed}/{len(sets)} runtime sets provide all {len(catalog)} named symbols '
          f'({len(catalog) - local} .dynsym-only, {local} any-named)')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
