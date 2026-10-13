#!/usr/bin/env python3
"""Check the named-symbol catalog against collected ART runtimes.

Every NamedSymbol in platform/symbols/structural_redefine_symbols.h must be a
defined name in its declared ELF tables in every collected runtime set.
--report writes per-set name/table results and content hashes only under
ignored build/evidence. No disassembly, symbol addresses, field offsets or
loader resolution is used.
"""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from elf_mini_inventory import mini_symbol_inventory, symbol_table_inventory

ROOT = Path(__file__).resolve().parent.parent
CATALOG = ROOT / 'artdcevm/src/main/cpp/platform/symbols/structural_redefine_symbols.h'
LIBRARIES = {'kJvmti': 'libopenjdkjvmti.so', 'kArt': 'libart.so'}
TABLES = ('.dynsym', '.symtab', '.gnu_debugdata')
ENTRY = re.compile(r'NamedSymbol\s+(k\w+)\{Library::(k\w+),\s*"([^"]+)"'
                   r'(?:,\s*SymbolTable::(k\w+))?\s*\}')
SCOPE = 'Defined exact names only; no address, ABI, loader or callability proof'


def read_catalog(path):
    content = path.read_bytes()
    source = content.decode('utf-8')
    catalog = ENTRY.findall(source)
    declarations = re.findall(r'\bNamedSymbol\s+k\w+\s*\{', source)
    if not catalog or len(catalog) != len(declarations):
        raise ValueError('Catalog has no entries or an unparsed NamedSymbol declaration')
    if {library for _, library, _, _ in catalog} - LIBRARIES.keys():
        raise ValueError('Catalog uses unsupported libraries')
    if {table for _, _, _, table in catalog} - {'', 'kDynsym', 'kAnyNamed'}:
        raise ValueError('Catalog uses unsupported symbol tables')
    if len({entry[0] for entry in catalog}) != len(catalog):
        raise ValueError('Catalog contains duplicate identifiers')
    if len({(entry[1], entry[2]) for entry in catalog}) != len(catalog):
        raise ValueError('Catalog contains duplicate exact names for a library')
    return catalog, hashlib.sha256(content).hexdigest()


def report_destination(path):
    if path is None:
        return None
    destination = path.resolve()
    try:
        relative = destination.relative_to(ROOT / 'build/evidence')
    except ValueError as error:
        raise ValueError('Report must be saved under ignored build/evidence') from error
    if not relative.parts or destination.suffix != '.json':
        raise ValueError('Report must be a JSON file under ignored build/evidence')
    return destination


def library_inventory(path):
    if not path.exists():
        return {'present': False, 'sha256': None, 'tableCounts': dict.fromkeys(TABLES, 0)}, {
            table: set() for table in TABLES}
    data = path.read_bytes()
    tables = symbol_table_inventory(data)
    mini_names, provenance = mini_symbol_inventory(data)
    tables['.gnu_debugdata'] = mini_names
    tables = {table: set(names) for table, names in tables.items()}
    metadata = {'present': True, 'sha256': hashlib.sha256(data).hexdigest(),
                'tableCounts': {table: len(names) for table, names in tables.items()},
                'miniProvenance': provenance}
    return metadata, tables


def name_result(name, tables):
    locations = [table for table in TABLES if name in tables[table]]
    return {'name': name, 'tables': locations, 'present': bool(locations)}


def summarize(sets, catalog):
    failed = sum(not all(entry['present'] for entry in runtime['catalog']) for runtime in sets)
    any_named = sum(table == 'kAnyNamed' for _, _, _, table in catalog)
    return {'totalSets': len(sets), 'passedSets': len(sets) - failed, 'namedSymbols': len(catalog),
            'dynsymOnly': len(catalog) - any_named, 'anyNamed': any_named}


def collect_report(runtime_root, catalog, catalog_hash):
    directories = sorted(path.parent for path in runtime_root.rglob('libopenjdkjvmti.so'))
    if not directories:
        raise ValueError('No collected runtime sets found')
    sets = []
    for number, directory in enumerate(directories, 1):
        # The fourth component can contain private device/build identifiers.
        # Set IDs and library hashes distinguish builds without publishing it.
        identifier = f'set-{number:02d}'
        label = '/'.join(directory.relative_to(runtime_root).parts[:3])
        inventories, metadata = {}, {}
        for library in sorted(LIBRARIES.values()):
            try:
                metadata[library], inventories[library] = library_inventory(directory / library)
            except (ValueError, OSError) as error:
                raise ValueError(f'{identifier}: unable to inventory {library}') from error
        entries = []
        for name, library, symbol, table in catalog:
            filename = LIBRARIES[library]
            result = name_result(symbol, inventories[filename])
            required = list(TABLES) if table == 'kAnyNamed' else ['.dynsym']
            entries.append({'id': name, 'library': filename, **result, 'requiredTables': required,
                            'present': bool(set(result['tables']) & set(required))})
        sets.append({'id': identifier, 'label': label, 'libraries': metadata, 'catalog': entries})
    return {'schemaVersion': 2, 'scope': SCOPE, 'catalogSha256': catalog_hash, 'sets': sets,
            'summary': summarize(sets, catalog)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-lib', type=Path, default=ROOT / 'assets/runtime_lib')
    parser.add_argument('--report', type=Path, help='JSON result file under ignored build/evidence')
    args = parser.parse_args(argv)
    try:
        destination = report_destination(args.report)
        catalog, catalog_hash = read_catalog(CATALOG)
        report = collect_report(args.runtime_lib, catalog, catalog_hash)
        if destination:
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    except (ValueError, OSError) as error:
        # OSError messages contain user paths; keep diagnostics anonymous.
        print(str(error) if isinstance(error, ValueError) else 'Unable to read inputs or write evidence report',
              file=sys.stderr)
        return 2

    for runtime in report['sets']:
        missing = [f"{entry['library']}:{entry['id']}" for entry in runtime['catalog'] if not entry['present']]
        status = 'ok' if not missing else 'missing ' + ', '.join(missing)
        print(f"{runtime['id']} {runtime['label']}: {status}")
    summary = report['summary']
    print(f"{summary['passedSets']}/{summary['totalSets']} runtime sets provide all {summary['namedSymbols']} "
          f"named symbols ({summary['dynsymOnly']} .dynsym-only, {summary['anyNamed']} any-named)")
    return 0 if summary['passedSets'] == summary['totalSets'] else 1


if __name__ == '__main__':
    sys.exit(main())
