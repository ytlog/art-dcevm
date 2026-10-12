#!/usr/bin/env python3
"""Regression checks for strict table requirements and exact-name research reports."""
import contextlib
import hashlib
import importlib.util
import io
import json
import lzma
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('symbol_audit', Path(__file__).with_name('check-structural-symbols.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def elf(dynamic=(), local=(), mini=None):
    """Build an AArch64 ELF with independent tables and irrelevant symbol values."""
    sections = [('', 0, b'', None, 0), ('.shstrtab', 3, b'', None, 0)]
    for table_name, kind, string_name, entries in (
            ('.dynsym', 11, '.dynstr', dynamic), ('.symtab', 2, '.strtab', local)):
        strings, records = b'\0', b'\0' * 24
        for name, defined in entries:
            at = len(strings)
            strings += name.encode() + b'\0'
            records += struct.pack('<IBBHQQ', at, 18, 0, 1 if defined else 0, 0xdeadbeef, 0xbeef)
        string_index = len(sections)
        sections.append((string_name, 3, strings, None, 0))
        sections.append((table_name, kind, records, string_index, 24))
    if mini is not None:
        sections.append(('.gnu_debugdata', 1, lzma.compress(mini), None, 0))
    strings, name_offsets = b'\0', {'': 0}
    for name, *_ in sections[1:]:
        name_offsets[name] = len(strings)
        strings += name.encode() + b'\0'
    sections[1] = ('.shstrtab', 3, strings, None, 0)
    header = struct.pack('<16sHHIQQQIHHHHHH', b'\x7fELF\x02\x01\x01' + b'\0' * 9,
                         3, 183, 1, 0, 0, 64, 0, 64, 0, 0, 64, len(sections), 1)
    offset, table, contents = 64 + 64 * len(sections), b'', b''
    for name, kind, data, link, stride in sections:
        table += struct.pack('<IIQQQQIIQQ', name_offsets[name], kind, 0, 0,
                             offset if kind else 0, len(data), link or 0, 0, 1, stride)
        contents += data
        offset += len(data)
    return header + table + contents


class SymbolAuditTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
        self.original_root, self.original_catalog = audit.ROOT, audit.CATALOG
        audit.ROOT = self.root
        audit.CATALOG = self.root / 'symbols.h'
        audit.CATALOG.write_text(
            'NamedSymbol kExported{Library::kArt, "exported"};\n'
            'NamedSymbol kWrapper{Library::kJvmti, "wrapper", SymbolTable::kAnyNamed};\n')
        self.runtime = self.root / 'runtime'
        self.directory = self.runtime / 'api-30' / 'OEM' / 'model' / 'private-device-build'
        self.directory.mkdir(parents=True)
        self.art = self.directory / 'libart.so'
        self.jvmti = self.directory / 'libopenjdkjvmti.so'
        self.art.write_bytes(elf(dynamic=[('exported', True), ('undefined', False)],
                                 local=[('exported', True), ('local', True)],
                                 mini=elf(local=[('mini', True), ('undefined', False)])))
        self.jvmti.write_bytes(elf(mini=elf(local=[('wrapper', True)])))

    def tearDown(self):
        audit.ROOT, audit.CATALOG = self.original_root, self.original_catalog
        self.temporary.cleanup()

    def run_check(self, *extra):
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            result = audit.main(['--runtime-lib', str(self.runtime), *map(str, extra)])
        return result, output.getvalue()

    def test_research_names_preserve_each_table_and_do_not_weaken_catalog(self):
        candidate = self.root / 'research.json'
        candidate.write_text(json.dumps({'services': [
            {'id': 'variants', 'library': 'kArt', 'names': ['local', 'mini', 'missing', 'undefined'],
             'source': 'official source'},
            {'id': 'inline', 'library': 'libart.so', 'names': [], 'inline': True}]}))
        report = self.root / 'build/evidence/audit.json'
        result, output = self.run_check('--audit-spec', candidate, '--report', report)
        self.assertEqual(result, 0)
        self.assertNotIn('private-device-build', output)
        content = report.read_text()
        self.assertNotIn('private-device-build', content)
        self.assertNotIn(str(self.root), content)
        data = json.loads(content)
        runtime = data['sets'][0]
        self.assertEqual(runtime['id'], 'set-01')
        self.assertEqual(runtime['label'], 'api-30/OEM/model')
        self.assertEqual(runtime['catalog'][0]['tables'], ['.dynsym', '.symtab'])
        self.assertEqual(runtime['catalog'][1]['tables'], ['.gnu_debugdata'])
        self.assertEqual([entry['tables'] for entry in runtime['services'][0]['names']],
                         [['.symtab'], ['.gnu_debugdata'], [], []])
        self.assertEqual(runtime['libraries']['libart.so']['sha256'], hashlib.sha256(self.art.read_bytes()).hexdigest())
        self.assertEqual(data['auditSpecSha256'], hashlib.sha256(candidate.read_bytes()).hexdigest())
        self.assertEqual(data['auditSpec']['services'][0]['source'], 'official source')
        self.assertTrue(runtime['services'][0]['available'])
        self.assertFalse(runtime['services'][1]['available'])
        self.assertTrue(data['summary']['services'][1]['inlineOnly'])
        self.assertEqual(data['summary']['services'][0]['variants'][2]['tableSetCounts']['missing'], 1)

    def test_catalog_dynsym_requirement_rejects_a_local_only_name(self):
        self.art.write_bytes(elf(local=[('exported', True)]))
        result, output = self.run_check()
        self.assertEqual(result, 1)
        self.assertIn('libart.so:kExported', output)

    def test_missing_library_is_catalog_failure_and_duplicate_builds_have_distinct_ids(self):
        self.art.unlink()
        second = self.directory.with_name('another-private-build')
        second.mkdir()
        (second / 'libopenjdkjvmti.so').write_bytes(self.jvmti.read_bytes())
        (second / 'libart.so').write_bytes(elf(dynamic=[('exported', True)]))
        report = self.root / 'build/evidence/audit.json'
        result, output = self.run_check('--report', report)
        self.assertEqual(result, 1)
        data = json.loads(report.read_text())
        self.assertEqual(len({runtime['id'] for runtime in data['sets']}), 2)
        self.assertEqual(len({runtime['label'] for runtime in data['sets']}), 1)
        self.assertIn('1/2 runtime sets', output)

    def test_catalog_rejects_unknown_tables_libraries_duplicates_and_unparsed_entries(self):
        for source in (
                'NamedSymbol kBad{Library::kOther, "bad"};',
                'NamedSymbol kBad{Library::kArt, "bad", SymbolTable::kOther};',
                'NamedSymbol kBad{Library::kArt, "bad"};\nNamedSymbol kBad{Library::kArt, "other"};',
                'NamedSymbol kBad{Library::kArt, "bad"};\nNamedSymbol kOther{Library::kArt, "bad"};',
                'NamedSymbol kBad{Library::kArt, "bad", malformed};'):
            with self.subTest(source=source):
                audit.CATALOG.write_text(source)
                result, _ = self.run_check()
                self.assertEqual(result, 2)

    def test_spec_rejects_bad_exact_names_libraries_and_duplicate_ids(self):
        candidate = self.root / 'research.json'
        cases = [{'services': [service]} for service in (
            {'id': 'valid', 'library': 'kArt', 'names': ['same', 'same']},
            {'id': 'valid', 'library': 'kArt', 'names': ['not exact']},
            {'id': 'valid', 'library': '../private.so', 'names': []},
            {'id': 'valid', 'library': 'kArt', 'names': 'bad'},
            {'id': 'bad id', 'library': 'kArt', 'names': []})]
        cases += [{'services': [{'id': 'same', 'library': 'kArt', 'names': []}] * 2}, {}]
        for content in cases:
            with self.subTest(content=content):
                candidate.write_text(json.dumps(content))
                result, _ = self.run_check('--audit-spec', candidate)
                self.assertEqual(result, 2)

    def test_report_rejects_paths_outside_ignored_evidence_and_symlink_escapes(self):
        result, output = self.run_check('--report', self.root / 'published.json')
        self.assertEqual(result, 2)
        self.assertNotIn(str(self.root), output)
        (self.root / 'build/evidence').mkdir(parents=True)
        (self.root / 'outside').mkdir()
        (self.root / 'build/evidence/link').symlink_to(self.root / 'outside', target_is_directory=True)
        result, _ = self.run_check('--report', self.root / 'build/evidence/link/audit.json')
        self.assertEqual(result, 2)

    def test_no_runtime_sets_and_bad_elf_fail_without_private_paths(self):
        self.jvmti.unlink()
        result, _ = self.run_check()
        self.assertEqual(result, 2)
        self.jvmti.write_bytes(b'invalid elf')
        result, output = self.run_check()
        self.assertEqual(result, 2)
        self.assertNotIn(str(self.root), output)
        self.assertNotIn('private-device-build', output)


if __name__ == '__main__':
    unittest.main()
