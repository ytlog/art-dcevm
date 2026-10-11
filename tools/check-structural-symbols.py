#!/usr/bin/env python3
"""Verify the structural-redefinition symbol catalog against collected ART libraries.

Every NamedSymbol must exist in its declared ELF symbol tables in each runtime
set. The standard-table wrapper also permits .symtab/.gnu_debugdata names.
This tool inventories names only, never resolves or infers addresses.
"""
import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path
from elf_mini_inventory import mini_symbol_inventory, _defined_names

ROOT = Path(__file__).resolve().parent.parent
CATALOG = ROOT / 'artdcevm/src/main/cpp/platform/symbols/structural_redefine_symbols.h'
LIBRARIES = {'kJvmti': 'libopenjdkjvmti.so', 'kArt': 'libart.so'}
ENTRY = re.compile(r'NamedSymbol\s+(k\w+)\{Library::(k\w+),\s*"([^"]+)"(?:,\s*SymbolTable::(k\w+))?\}')


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


def dynamic_symbols(nm, library):
    output = subprocess.run([str(nm), '-D', '--defined-only', str(library)],
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
    unknown_tables = {table for _, _, _, table in catalog} - {'', 'kDynamic', 'kAnyNamed'}
    if unknown_tables:
        sys.exit(f'catalog uses unsupported symbol tables: {sorted(unknown_tables)}')

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
            key = (library, table)
            if key not in cache:
                names = dynamic_symbols(nm, path) if path.exists() else set()
                if table == 'kAnyNamed' and path.exists():
                    data = path.read_bytes()
                    names.update(_defined_names(data))
                    names.update(mini_symbol_inventory(data)[0])
                cache[key] = names
            if symbol not in cache[key]:
                missing.append(f'{LIBRARIES[library]}:{name}')
        status = 'ok' if not missing else 'missing ' + ', '.join(missing)
        print(f"{'/'.join(label)}: {status}")
        failed += bool(missing)

    print(f'{len(sets) - failed}/{len(sets)} runtime sets provide all {len(catalog)} symbols')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
